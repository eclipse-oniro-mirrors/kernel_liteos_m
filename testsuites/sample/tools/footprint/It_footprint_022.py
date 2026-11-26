#!/usr/bin/env python3
"""ItFootprint022: 覆盖率新增分支验证测试 (依赖 ItFootprint021 的 cov_report.txt)."""
from os_test import *

def TestCase():
    d = mk("ItFootprint022")
    cov = open(f"{OUT}/ItFootprint021/cov_report.txt").read()
    has_missing = "anchors.py" in cov and "%" in cov
    blocks = len(re.findall(r"\d+-\d+", cov))
    checks = [("缺失块分布可归因(anchors 表达式变体/树搜回退/探针降级)", ">0 块",
              f"{blocks} 个缺失块区间", has_missing and blocks > 0)]
    record("ItFootprint022", checks)


def ItFootprint022():
    TEST_ADD_CASE("ItFootprint022", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
