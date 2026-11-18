#!/usr/bin/env python3
"""ItFootprint004: --cc 显式指定工具链测试."""
from os_test import *

def TestCase():
    d = mk("it004")
    r = run(["--out", QOUT, "--cc", ARMCC, "--output", f"{d}/rpt"])
    checks = [("rc", "0", str(r.returncode), r.returncode == 0),
              ("probe 点名显式编译器路径", "含绝对路径",
               "在位" if ARMCC in r.stdout else "缺失", ARMCC in r.stdout)]
    open(f"{d}/stdout.txt", "w").write(r.stdout + r.stderr)
    record("ItFootprint004", checks)


def ItFootprint004():
    TEST_ADD_CASE("ItFootprint004", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
