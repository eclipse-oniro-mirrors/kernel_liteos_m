#!/usr/bin/env python3
"""ItStackusage021: D4 大 SCC 有界性测试 (补充 U5, ring40 边界/压力)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage021")
    r = run(["--asm", f"{FX}/ring40.asm", "-j"])
    j = load_json(d) if r.returncode == 0 else {}
    allrec = all(v.get("IsRec") for v in j.values()) if j else False
    all320 = all(v.get("MaxCost") == 320 for v in j.values()) if j else False
    checks = [("rc=0(预算内不崩溃)", "0", str(r.returncode), r.returncode == 0),
              ("40 成员", "40", str(len(j)), len(j) == 40),
              ("全员 IsRec", "True", str(allrec), allrec),
              ("全员 Max=320(8×40 精确满环和)", "320", str(all320), all320)]
    record("ItStackusage021", checks)


def ItStackusage021():
    TEST_ADD_CASE("ItStackusage021", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
