#!/usr/bin/env python3
"""ItStackusage019: D9 局部标号过滤测试 (补充 U3)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage019")
    r = run(["--asm", f"{FX}/arm_syn2.asm", "-j"])
    m = funcs(load_json(d)) if r.returncode == 0 else {}
    e = m.get("FuncL", [{}])[0]
    checks = [("FuncL Max=4", "4", str(e.get("MaxCost")), e.get("MaxCost") == 4),
              ("局部标号不建调用边 Called=[]", "[]", str(e.get("Called")), e.get("Called") == [])]
    record("ItStackusage019", checks)


def ItStackusage019():
    TEST_ADD_CASE("ItStackusage019", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
