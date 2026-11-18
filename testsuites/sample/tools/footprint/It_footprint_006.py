#!/usr/bin/env python3
"""ItFootprint006: report.md 十节结构与留痕测试."""
from os_test import *

def TestCase():
    d = mk("ItFootprint006")
    r = open(f"{OUT}/it003/rpt/report.md").read()
    secs = ["统计说明", "汇总", "子模块明细", "动态申请明细", "动态项分类总览", "内存池布局",
            "排除项", "段对账", "告警", "输入与一致性"]
    miss = [s for s in secs if s not in r]
    import zipfile
    z = zipfile.ZipFile(f"{OUT}/it003/rpt/footprint.xlsx")
    ntabs = len(z.namelist())
    checks = [("十节齐全", "齐全", "缺 " + str(miss) if miss else "齐全", not miss),
              ("板级宏合并留痕(+N 宏)", "在位", "在位" if re.search(r"\+\d+ 宏", r) else "缺失",
               bool(re.search(r"\+\d+ 宏", r))),
              ("sizeof 来源: probe 编译实测", "在位", "在位" if "probe 编译实测" in r else "缺失",
               "probe 编译实测" in r),
              ("xlsx ≥5 页签", "≥5", str(ntabs), ntabs >= 5)]
    record("ItFootprint006", checks)


def ItFootprint006():
    TEST_ADD_CASE("ItFootprint006", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
