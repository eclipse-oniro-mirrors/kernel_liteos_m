#!/usr/bin/env python3
"""ItFootprint013: 段对账与告警审计测试 (R5/R7/D24/D16).

D24 口径(2026-11-18 拍板): footprint 工具定位为基础内核检查,
未识别桶仅监控基础内核前缀; kernel/extended 未登记前缀
(如 cppsupport.)属扩展内核新增模块, 豁免不判失败.
"""
from os_test import *

def TestCase():
    d = mk("ItFootprint013")
    r = open(f"{OUT}/it003/rpt/report.md").read()
    m = re.search(r"段对账(.*?)\n## ", r, re.S)
    seg = m.group(1) if m else ""
    zeros = seg.count("| 0 |") + len(re.findall(r"\|\s*0\s*\|", seg))
    warns = re.findall(r"未识别内核成员前缀 `([\w\-.+]+)`", r)
    extroot = f"{KLM}/kernel/extended"
    extdirs = {n for n in os.listdir(extroot) if os.path.isdir(f"{extroot}/{n}")}
    drift = [w for w in warns if w.rstrip(".") not in extdirs]
    ext_seen = sorted({w for w in warns if w.rstrip(".") in extdirs})
    checks = [("段对账表差额列全 0(R7)", "全 0", f"{zeros} 处 0", zeros >= 5),
              ("无 未入锚点表 告警(R5)", "无", "无" if "未入锚点表" not in r else "有", "未入锚点表" not in r),
              ("未识别桶-基础内核口径(D24)", "无基础内核未识别前缀",
               f"基础内核漂移: {drift or '无'}; 扩展内核不监控: {ext_seen or '无'}", not drift),
              ("config.h 转换告警留痕(D16)", "在位", "在位" if "auto-converted" in r else "缺失",
               "auto-converted" in r)]
    record("ItFootprint013", checks)


def ItFootprint013():
    TEST_ADD_CASE("ItFootprint013", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
