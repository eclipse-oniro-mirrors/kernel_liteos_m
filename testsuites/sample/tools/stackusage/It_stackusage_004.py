#!/usr/bin/env python3
"""ItStackusage004: config.ini 间接调用补全闭环测试 (W4, D6/D7 幂等)."""
from os_test import *
import hashlib

def TestCase():
    d = mk("ItStackusage004")
    shutil.rmtree(f"{d}/out", ignore_errors=True)
    shutil.rmtree(f"{d}/cfg", ignore_errors=True)
    r1 = run(["--asm", f"{FX}/arm_syn.asm", "-j"])
    cfg = f"{d}/cfg/config.ini"
    body = open(cfg).read() if os.path.isfile(cfg) else ""
    ok_stub = "Miss_funcptr:<FuncG>:0x484" in body and "Miss_funcptr:<FuncH>:0x604" in body
    body = body.replace("Remarks_here:<FuncG>:0x484\n", "Remarks_here:<FuncG>:0x484:FuncB\n")
    body = body.replace("Remarks_here:<FuncH>:0x604\n", "Remarks_here:<FuncH>:0x604:dup\n")
    open(cfg, "w").write(body)
    r2 = run(["--asm", f"{FX}/arm_syn.asm", "-j"])
    m = funcs(load_json(d)) if r2.returncode == 0 else {}
    md5_1 = hashlib.md5(open(cfg, "rb").read()).hexdigest()
    run(["--asm", f"{FX}/arm_syn.asm", "-j"])
    md5_2 = hashlib.md5(open(cfg, "rb").read()).hexdigest()
    checks = [("首跑空桩成对", "2 对", "在" if ok_stub else "无", ok_stub),
              ("补全后告警清零", "0", str(r2.stdout.count("Miss_funcptr")),
               r2.stdout.count("Miss_funcptr") == 0),
              ("FuncG Max=24(8+16)", "24", str(m.get("FuncG", [{}])[0].get("MaxCost")),
               m.get("FuncG", [{}])[0].get("MaxCost") == 24),
              ("FuncH Max=28(4+max(8,24))", "28", str(m.get("FuncH", [{}])[0].get("MaxCost")),
               m.get("FuncH", [{}])[0].get("MaxCost") == 28),
              ("同名双实例全展开", "['0x700', '0x800']",
               str(sorted(m.get("FuncH", [{}])[0].get("Called", []))),
               sorted(m.get("FuncH", [{}])[0].get("Called", [])) == ["0x700", "0x800"]),
              ("config md5 幂等", "一致", "一致" if md5_1 == md5_2 else "漂移", md5_1 == md5_2)]
    record("ItStackusage004", checks)


def ItStackusage004():
    TEST_ADD_CASE("ItStackusage004", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
