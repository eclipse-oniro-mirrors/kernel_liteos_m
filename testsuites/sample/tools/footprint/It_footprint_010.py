#!/usr/bin/env python3
"""ItFootprint010: --sizeof-table 离线复现测试 (依赖 gen_inputs.py 产出的 it010/sizeof_inject.json)."""
from os_test import *

def TestCase():
    d = mk("it010")
    inject = f"{OUT}/it010/sizeof_inject.json"
    r = run(["--out", QOUT, "--sizeof-table", inject, "--output", f"{d}/rpt_inject"],
            env={"PATH": "/nonexistent"})
    jp = json.load(open(f"{OUT}/it003/rpt/summary.json"))["totals"]
    ji = json.load(open(f"{d}/rpt_inject/summary.json"))["totals"]
    five = ["ram_static", "stack", "dyn", "overhead", "ram_total"]
    same = all(jp[k] == ji[k] for k in five)
    rpt = open(f"{d}/rpt_inject/report.md").read()
    checks = [("无编译器环境 rc", "0", str(r.returncode), r.returncode == 0),
              ("stdout 无 probe 行", "无", "无" if "values measured" not in r.stdout else "有",
               "values measured" not in r.stdout),
              ("报告头 sizeof 来源=内部注入", "在位", "在位" if "内部注入" in rpt else "缺失", "内部注入" in rpt),
              ("注入与探针五指标一致", "一致", "一致" if same else "不一致", same)]
    open(f"{d}/stdout.txt", "w").write(r.stdout + r.stderr)
    record("ItFootprint010", checks)


def ItFootprint010():
    TEST_ADD_CASE("ItFootprint010", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
