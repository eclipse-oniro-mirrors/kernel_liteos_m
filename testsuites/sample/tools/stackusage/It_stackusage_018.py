#!/usr/bin/env python3
"""ItStackusage018: D8 条件指令测试 (补充 U2, subeq 计入/popeq 视为未知)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage018")
    r = run(["--asm", f"{FX}/arm_syn2.asm", "-j"])
    m = funcs(load_json(d)) if r.returncode == 0 else {}
    checks = [("FuncY Local=24(8+16, subeq 保守计入)", "24",
               str(m.get("FuncY", [{}])[0].get("LocalCost")),
               m.get("FuncY", [{}])[0].get("LocalCost") == 24),
              ("FuncZ Max=124(24+100, popeq 为普通边)", "124",
               str(m.get("FuncZ", [{}])[0].get("MaxCost")),
               m.get("FuncZ", [{}])[0].get("MaxCost") == 124)]
    record("ItStackusage018", checks)


def ItStackusage018():
    TEST_ADD_CASE("ItStackusage018", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
