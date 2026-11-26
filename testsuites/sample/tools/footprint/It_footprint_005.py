#!/usr/bin/env python3
"""ItFootprint005: summary.json 契约测试."""
from os_test import *

def TestCase():
    d = mk("ItFootprint005")
    j = json.load(open(f"{OUT}/it003/rpt/summary.json"))
    inputs_keys = set(j.get("inputs", {}))
    totals_keys = set(j.get("totals", {}))
    need_in = {"map", "map_mtime", "config", "config_mtime"}
    need_tot = {"ram_static", "stack", "heap", "dyn", "overhead", "ram_total", "ram_total_overhead"}
    groups = j.get("groups", {})
    numeric = all(isinstance(v, (int, float)) for v in j.get("totals", {}).values()) and \
              all(isinstance(v, (int, float)) for g in groups.values() for v in (g.values() if isinstance(g, dict) else [g]))
    checks = [("inputs 四键", str(sorted(need_in)), str(sorted(inputs_keys)), need_in <= inputs_keys),
              ("totals 七键", str(sorted(need_tot)), str(sorted(totals_keys)), need_tot <= totals_keys),
              ("groups 非空组覆盖", "11~12 组", f"{len(groups)} 组", len(groups) >= 11),
              ("数值型", "是", "是" if numeric else "否", numeric)]
    json.dump(j, open(f"{d}/summary_echo.json", "w"), indent=1)
    record("ItFootprint005", checks)


def ItFootprint005():
    TEST_ADD_CASE("ItFootprint005", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
