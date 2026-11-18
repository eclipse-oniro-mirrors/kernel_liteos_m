#!/usr/bin/env python3
"""ItFootprint003: --out 零参数一键测试 (it003 产物为 005/006/008/013 后继用例输入)."""
from os_test import *

def TestCase():
    d = mk("it003")
    r = run(["--out", QOUT, "--output", f"{d}/rpt"])
    o = r.stdout
    tri = all(os.path.exists(f"{d}/rpt/{f}") for f in ("report.md", "footprint.xlsx", "summary.json"))
    checks = [("rc", "0", str(r.returncode), r.returncode == 0),
              ("inputs 两行(config 带 auto-converted)", "2 行", str(o.count("inputs:")) + " 行含 " + str("auto-converted" in o),
               "auto-converted from config.h" in o),
              ("probe 回显 16 values measured", "在位", "在位" if "16 values measured" in o else "缺失",
               "16 values measured" in o),
              ("RAM 构成+合计三行", "在位", "在位" if ("RAM 构成" in o and "RAM 合计" in o) else "缺失",
               "RAM 构成" in o and "RAM 合计" in o),
              ("report written 末行", "在位", "在位" if "report written" in o else "缺失", "report written" in o),
              ("三件套产物", "在位", "在位" if tri else "缺失", tri)]
    open(f"{d}/stdout.txt", "w").write(o + r.stderr)
    record("ItFootprint003", checks)


def ItFootprint003():
    TEST_ADD_CASE("ItFootprint003", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
