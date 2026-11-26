#!/usr/bin/env python3
"""ItStackusage017: D2 保守语义测试 (补充 U1, 未知栈操作不算释放)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage017")
    r = run(["--asm", f"{FX}/arm_syn2.asm", "-j"])
    m = funcs(load_json(d)) if r.returncode == 0 else {}
    checks = [("FuncX Max=108(8+100, mov sp,r8 保守)", "108",
               str(m.get("FuncX", [{}])[0].get("MaxCost")),
               m.get("FuncX", [{}])[0].get("MaxCost") == 108)]
    record("ItStackusage017", checks)


def ItStackusage017():
    TEST_ADD_CASE("ItStackusage017", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
