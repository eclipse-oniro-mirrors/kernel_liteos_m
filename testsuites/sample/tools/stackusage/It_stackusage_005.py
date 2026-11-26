#!/usr/bin/env python3
"""ItStackusage005: Ignore_function 整体忽略测试 (W5)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage005")
    open(f"{d}/my.ini", "w").write("Ignore_function: FuncB\n")
    r = run(["--asm", f"{FX}/arm_syn.asm", "-j", "--config", f"{d}/my.ini"])
    m = funcs(load_json(d)) if r.returncode == 0 else {}
    checks = [("rc=0", "0", str(r.returncode), r.returncode == 0),
              ("FuncB 帧与出边清零", "0/0",
               f"{m.get('FuncB', [{}])[0].get('LocalCost')}/{m.get('FuncB', [{}])[0].get('MaxCost')}",
               (m.get("FuncB", [{}])[0].get("LocalCost"), m.get("FuncB", [{}])[0].get("MaxCost")) == (0, 0)),
              ("FuncA Max=36(36+0)", "36", str(m.get("FuncA", [{}])[0].get("MaxCost")),
               m.get("FuncA", [{}])[0].get("MaxCost") == 36)]
    record("ItStackusage005", checks)


def ItStackusage005():
    TEST_ADD_CASE("ItStackusage005", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
