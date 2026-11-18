#!/usr/bin/env python3
"""ItFootprint007: an386 真值锚点测试 (树演进数值漂移以实际为准)."""
from os_test import *

def TestCase():
    d = mk("ItFootprint007")
    j = json.load(open(f"{OUT}/it003/rpt/summary.json"))
    t = j["totals"]
    exp = {"ram_static": 16961, "stack": 4096, "dyn": 32928, "overhead": 288, "ram_total": 53985}
    act = {k: t.get(k) for k in exp}
    checks = [(f"{k}", str(v), str(act[k]), act[k] == v) for k, v in exp.items()]
    record("ItFootprint007", checks)


def ItFootprint007():
    TEST_ADD_CASE("ItFootprint007", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
