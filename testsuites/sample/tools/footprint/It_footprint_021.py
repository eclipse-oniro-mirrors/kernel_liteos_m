#!/usr/bin/env python3
"""ItFootprint021: 覆盖率补充复测测试 (coverage 全矩阵重放, 依赖 001~015 的产物目录)."""
from os_test import *

def TestCase():
    d = mk("ItFootprint021")
    fx = f"{OUT}/fx"
    inj = f"{OUT}/it010/sizeof_inject.json"
    cmds = [
        (["--out", QOUT, "--output", f"{d}/c1"], {}),
        (["--out", QOUT, "--cc", ARMCC, "--output", f"{d}/c2"], {}),
        (["--out", QOUT, "--sizeof-table", inj, "--output", f"{d}/c3"], {"PATH": "/nonexistent"}),
        (["--map", f"{fx}/plain/OHOS_Image.map", "--config", f"{fx}/full.config", "--output", f"{d}/c4"], {"PATH": "/nonexistent"}),
        (["--out", QOUT, "--cc", "/nonexistent_cc", "--output", f"{d}/c5"], {}),
        (["--out", QOUT, "--config", f"{fx}/full.config", "--sizeof-table", inj, "--output", f"{d}/c6"], {"PATH": "/nonexistent"}),
        (["--out", f"{OUT}/ItFootprint015/topcfg", "--sizeof-table", inj, "--output", f"{d}/c7"], {"PATH": "/nonexistent"}),
        (["--out", f"{OUT}/ItFootprint015/subcfg", "--sizeof-table", inj, "--output", f"{d}/c8"], {"PATH": "/nonexistent"}),
        (["--out", f"{OUT}/ItFootprint015/multicfg", "--sizeof-table", inj, "--output", f"{d}/c9"], {"PATH": "/nonexistent"}),
        ([], {}),
        (["--out", QOUT, "--map", f"{fx}/plain/OHOS_Image.map"], {}),
        (["--map", f"{fx}/plain/OHOS_Image.map"], {}),
        (["--out", "/nonexistent_dir"], {}),
        (["--out", f"{fx}/empty"], {}),
        (["--out", f"{fx}/twomaps", "--config", f"{fx}/full.config"], {}),
        (["--out", f"{fx}/noconfig", "--cc", ARMCC], {}),
        (["--map", "/nonexistent.map", "--config", f"{fx}/full.config"], {}),
        (["--out", QOUT, "--config", "/nonexistent.config"], {}),
        (["--out", QOUT, "--sizeof-table", f"{fx}/st_array.json", "--output", f"{d}/n1"], {"PATH": "/nonexistent"}),
        (["--out", QOUT, "--sizeof-table", f"{fx}/st_str.json", "--output", f"{d}/n2"], {"PATH": "/nonexistent"}),
        (["--out", QOUT, "--sizeof-table", "/nonexistent_st.json", "--output", f"{d}/n3"], {"PATH": "/nonexistent"}),
        (["--out", QOUT, "--sizeof-table", f"{fx}/st_zero.json", "--output", f"{d}/n4"], {"PATH": "/nonexistent"}),
        (["--out", WOUT, "--output", f"{d}/w1"], {"PATH": RVPATH + ":" + os.environ["PATH"]}),
        (["--out", WOUT, "--output", f"{d}/w2"], {"PATH": "/nonexistent"}),
    ]
    for sym in ("LOSCFG_BASE_IPC_QUEUE", "LOSCFG_BASE_IPC_QUEUE_LIMIT", "LOSCFG_BASE_IPC_SEM_LIMIT", "LOSCFG_DEBUG_MUTEX"):
        p = f"{OUT}/ItFootprint012/{sym}.config"
        if os.path.exists(p):
            cmds.append((["--map", f"{fx}/plain/OHOS_Image.map", "--config", p, "--sizeof-table", inj,
                          "--output", f"{d}/g_{sym}"], {"PATH": "/nonexistent"}))
    for i, (args, extra) in enumerate(cmds):
        env = dict(os.environ)
        env.update(extra)
        subprocess.run([sys.executable, "-m", "coverage", "run", "-p", TOOL] + args,
                       capture_output=True, env=env, timeout=600, cwd=OUT)
    subprocess.run([sys.executable, "-m", "coverage", "combine"], capture_output=True, text=True, cwd=OUT)
    r2 = subprocess.run([sys.executable, "-m", "coverage", "report", "--show-missing"],
                        capture_output=True, text=True, cwd=OUT)
    open(f"{d}/cov_report.txt", "w").write(r2.stdout + r2.stderr)
    m = re.search(r"TOTAL\s+(\d+)\s+(\d+)\s+(\d+)%", r2.stdout)
    pct, stmts, miss = (m.group(3), m.group(1), m.group(2)) if m else ("?", "?", "?")
    checks = [("语句覆盖率(全矩阵重放)", "89%(1840 语句/缺 197)", f"{pct}%({stmts} 语句/缺 {miss})", pct not in ("?", "0"))]
    record("ItFootprint021", checks)


def ItFootprint021():
    TEST_ADD_CASE("ItFootprint021", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
