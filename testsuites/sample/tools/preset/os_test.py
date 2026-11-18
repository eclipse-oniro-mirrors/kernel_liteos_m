#!/usr/bin/env python3
"""preset 模块共享测试设施 (ICUNIT osTest.h/osTest.c 载体的 python 镜像).

供 It_preset_NNN.py 用例文件与本模块目录下的套件统一引用:
from os_test import *   (等效 ICUNIT 用例的 #include "osTest.h")

iCunit 镜像 (对齐 testsuites/include/iCunit.h + src/iCunit.c):
  - TEST_ADD_CASE(name, casefunc, layer, module, level, type)   用例自注册
  - ICunitRun()   逐用例计时执行, 板端同款 [Passed]/[Failed]/[SlowCase] 输出
  - Case Cost Time TOP 10 耗时排行, 每用例耗时在日志中可搜索
  - 失败行格式: [Failed]-<name>-<layer>-<module>-<level>-<type>-[Errline: N RetCode:0xM]
    (Errline = 用例内 record() 调用行号; RetCode = 首个失败检查项序号; 异常 = 0x0EF00001)

路径按 __file__ 相对解析, 不绑定机器绝对路径。
"""
import os, sys, time, fcntl, termios, struct, pty, hashlib, subprocess, shutil, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../../../../.."))
KLM = f"{ROOT}/kernel/liteos_m"
OUT = f"{ROOT}/testcase/run_20261112/preset"
QEMU_VENDOR = f"{ROOT}/vendor/ohemu/qemu_mini_system_demo/kernel_configs/debug.config"
WS63_VENDOR = f"{ROOT}/vendor/hihope/ws63_m_testsuits/kernel_configs/debug.config"
KENV = {"CONFIG_": "LOSCFG_", "srctree": KLM, "PATH": os.environ["PATH"], "HOME": os.environ["HOME"]}

# ---- iCunit 镜像常量 (对应 icunit_layer_def.h / icunit_module_def.h / icunit_level_def.h / icunit_type_def.h) ----
TEST_LOS = "TEST_LOS"
TEST_FN_BUILDTOOL_RAMPRESET = "TEST_FN_BUILDTOOL_RAMPRESET"
TEST_LEVEL0 = "TEST_LEVEL0"
TEST_FUNCTION = "TEST_FUNCTION"
ICUNIT_SUCCESS = 0x00000000
ICUNIT_CASE_EXC = 0x0EF00001
ICUNIT_CASE_TIME_TOP_N = 10
ICUNIT_CASE_TIME_SLOW_US = 1000000
ICUNIT_FAIL_LOG_MAX = 50

ICUNIT_CASE_ARRAY = []   # 镜像 iCunit_CaseArray: (name, func, layer, module, level, type)
_RESULTS = {}            # name -> True(过)/False(败)/None(skip)
_ERRINFO = {}            # name -> (errLine, retCode)
_SKIPINFO = {}           # name -> skip 原因
_TIME_TOP = []           # [(costUs, name)] 镜像 g_caseTimeTop

def log(msg):
    print(msg, flush=True)

def sh(cmd, cwd=KLM, env=None, timeout=120):
    e = dict(os.environ)
    if env: e.update(env)
    return subprocess.run(cmd, cwd=cwd, env=e, capture_output=True, text=True, timeout=timeout)

def kload(config):
    os.environ.update(KENV)
    from kconfiglib import Kconfig
    k = Kconfig(filename=f"{KLM}/Kconfig", warn=False)
    k.warn = False
    k.load_config(config)
    return k

def tri(k, name):
    s = k.syms.get(name)
    return None if s is None else s.tri_value

def ival(k, name):
    s = k.syms.get(name)
    return None if s is None else s.str_value

def md5(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()

def pty_make(target, config, timeout=90):
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(KLM)
        os.environ["TERM"] = "xterm"
        os.execvp("make", ["make", target, f"CONFIG={config}"])
        os._exit(127)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    out = b""
    deadline = time.time() + timeout
    sent_q = sent_y = False
    while True:
        if time.time() > deadline:
            os.kill(pid, 9)
            os.waitpid(pid, 0)
            return 124, out.decode(errors="replace")
        try:
            import select
            r, _, _ = select.select([fd], [], [], 0.5)
            if r:
                d = os.read(fd, 65536)
                if not d:
                    break
                out += d
        except OSError:
            break
        if not sent_q and time.time() > deadline - timeout + 3:
            os.write(fd, b"q"); sent_q = True
        if sent_q and not sent_y and time.time() > deadline - timeout + 4.5:
            os.write(fd, b"y"); sent_y = True
    _, status = os.waitpid(pid, 0)
    rc = os.waitstatus_to_exitcode(status)
    return rc, out.decode(errors="replace")

def ICunitAddCase(name, casefunc, testcase_layer, testcase_module, testcase_level, testcase_type):
    ICUNIT_CASE_ARRAY.append((name, casefunc, testcase_layer, testcase_module, testcase_level, testcase_type))
    return ICUNIT_SUCCESS

def TEST_ADD_CASE(name, casefunc, testcase_layer, testcase_module, testcase_level, testcase_type):
    """镜像 iCunit.h TEST_ADD_CASE: 用例自注册, 由 ICunitRun 统一执行."""
    uwRet = ICunitAddCase(name, casefunc, testcase_layer, testcase_module, testcase_level, testcase_type)
    assert uwRet == ICUNIT_SUCCESS, uwRet

def _module_num(module):
    """镜像 icunit_module_def.h 枚举序(Executed Model_Num 口径), 失败回退 -1."""
    try:
        src = open(f"{KLM}/testsuites/include/icunit_module_def.h").read()
        return re.findall(r"ICUNIT_MOD\((\w+)\)", src).index(module)
    except Exception:
        return -1

def record(case, checks):
    d = f"{OUT}/{case}"
    os.makedirs(d, exist_ok=True)
    lines = [f"# {case} replay result", "", "| 检查项 | 预期 | 实际 | 判定 |", "|---|---|---|---|"]
    allok = True
    for name, exp, act, ok in checks:
        lines.append(f"| {name} | {exp} | {act} | {'PASS' if ok else 'FAIL'} |")
        allok = allok and ok
    lines.append(f"\n结论: {'符合预期' if allok else '存在偏差(见上行)'}")
    open(f"{d}/result.md", "w").write("\n".join(lines) + "\n")
    _RESULTS[case] = allok
    if not allok:
        first = next(i for i, c in enumerate(checks) if not c[3]) + 1
        _ERRINFO[case] = (sys._getframe(1).f_lineno, first)

def skip(case, reason):
    _RESULTS[case] = None
    _SKIPINFO[case] = reason
    d = f"{OUT}/{case}"
    os.makedirs(d, exist_ok=True)
    open(f"{d}/result.md", "w").write(f"# {case} replay result\n\n结论: SKIP ({reason})\n")

def mkcase(name):
    os.makedirs(f"{OUT}/{name}", exist_ok=True)
    return f"{OUT}/{name}"

def ICunitRun(only=None):
    """镜像 ICunitRunF + ICunitPrintReport: 板端同款逐用例输出与耗时排行, 返回失败数."""
    passCnt = failCnt = skipCnt = 0
    failLogs = []
    del _TIME_TOP[:]
    for name, func, layer, module, level, ctype in list(ICUNIT_CASE_ARRAY):
        if only and name not in only:
            continue
        start = time.monotonic()
        try:
            func()
        except Exception as e:
            tb = sys.exc_info()[2]
            errLine = tb.tb_lineno if tb is not None else 0
            _RESULTS[name] = False
            _ERRINFO.setdefault(name, (errLine, ICUNIT_CASE_EXC))
            log(f"[{name}] EXC {e!r}")
        costUs = int((time.monotonic() - start) * 1000000)
        _TIME_TOP.append((costUs, name))
        _TIME_TOP.sort(reverse=True)
        del _TIME_TOP[ICUNIT_CASE_TIME_TOP_N:]
        if costUs > ICUNIT_CASE_TIME_SLOW_US:
            log(f"[SlowCase]-{name}-[Cost: {costUs // 1000000}.{costUs % 1000000:06d} s]")
        r = _RESULTS.get(name, False)
        eLine, eCode = _ERRINFO.get(name, (0, 0))
        if r is True:
            passCnt += 1
            log(f"  [Passed]-{name}-{layer}-{module}-{level}-{ctype}")
        elif r is None:
            skipCnt += 1
            log(f"  [Skipped]-{name}-{layer}-{module}-{level}-{ctype}")
        else:
            failCnt += 1
            failLogs.append((name, layer, module, level, ctype, eLine, eCode))
            log(f"  [Failed]-{name}-{layer}-{module}-{level}-{ctype}-[Errline: {eLine} RetCode:0x{eCode:x}]")
    log(f"failed count:{failCnt}, success count:{passCnt}")
    log(f"\n========== Failed Case Logs ({failCnt} / {ICUNIT_FAIL_LOG_MAX}) ==========")
    if ICUNIT_CASE_ARRAY:
        module = ICUNIT_CASE_ARRAY[0][3]
        log(f"Executed Model: {module}, Executed Model_Num: {_module_num(module)}, "
            f"failed_count: {failCnt}, success_count: {passCnt}")
        for name, layer, module2, level, ctype, eLine, eCode in failLogs:
            log(f"  [Failed]-{name}-{layer}-{module2}-{level}-{ctype}-[Errline: {eLine} RetCode:0x{eCode:x}]")
    log("\n Not Executed Model: ")
    log("\n========== End of Failed Case Logs ==========")
    log(f"\n========== Case Cost Time TOP {len(_TIME_TOP)} ==========")
    for i, (us, n) in enumerate(_TIME_TOP):
        log(f"TOP{i + 1}: {n:<46} {us // 1000000}.{us % 1000000:06d} s")
    log("========== End of Case Cost Time TOP ==========")
    return failCnt
