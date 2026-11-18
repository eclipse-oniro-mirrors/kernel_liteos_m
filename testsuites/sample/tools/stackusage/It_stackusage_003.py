#!/usr/bin/env python3
"""ItStackusage003: -r 递归放大语义测试 (W3, D5 N×frame)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage003")
    r = run(["--asm", f"{FX}/arm_syn.asm", "-j", "-r", "3", "-sp", "-t", "FuncR,FuncM"])
    m = funcs(load_json(d)) if r.returncode == 0 else {}
    checks = [("rc=0", "0", str(r.returncode), r.returncode == 0),
              ("FuncR Max=36(3×12)", "36", str(m.get("FuncR", [{}])[0].get("MaxCost")),
               m.get("FuncR", [{}])[0].get("MaxCost") == 36),
              ("FuncM Max=84(>r1 28)", "84", str(m.get("FuncM", [{}])[0].get("MaxCost")),
               m.get("FuncM", [{}])[0].get("MaxCost") == 84),
              ("FuncN Max=84", "84", str(m.get("FuncN", [{}])[0].get("MaxCost")),
               m.get("FuncN", [{}])[0].get("MaxCost") == 84)]
    record("ItStackusage003", checks)


def ItStackusage003():
    TEST_ADD_CASE("ItStackusage003", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
