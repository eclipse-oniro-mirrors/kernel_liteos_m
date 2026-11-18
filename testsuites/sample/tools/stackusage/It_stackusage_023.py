#!/usr/bin/env python3
"""ItStackusage023: 同名多实例单点展开测试 (补充 U8, D7)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage023")
    r = run(["--asm", f"{FX}/arm_syn.asm", "-sp", "-t", "dup", "-out"])
    rows = [ln for ln in r.stdout.splitlines() if ln.startswith("dup ")]
    checks = [("rc=0", "0", str(r.returncode), r.returncode == 0),
              ("表格双实例两行", "2", str(len(rows)), len(rows) == 2),
              ("峰值 24 与 8 各一行", "24/8",
               "/".join(ln.split()[1] for ln in rows),
               sorted(ln.split()[1] for ln in rows) == ["24", "8"])]
    record("ItStackusage023", checks)


def ItStackusage023():
    TEST_ADD_CASE("ItStackusage023", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
