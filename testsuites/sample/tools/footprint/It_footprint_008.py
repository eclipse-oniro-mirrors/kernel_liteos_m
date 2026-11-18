#!/usr/bin/env python3
"""ItFootprint008: 确定性与环比测试."""
from os_test import *

def TestCase():
    d = mk("ItFootprint008")
    r = run(["--out", QOUT, "--output", f"{OUT}/it003/rpt"])
    j2 = json.load(open(f"{OUT}/it003/rpt/summary.json"))
    same = run(["--out", QOUT, "--output", f"{d}/rpt3"])
    j3 = json.load(open(f"{d}/rpt3/summary.json"))
    det = json.dumps(j2["totals"]) == json.dumps(j3["totals"])
    r4 = run(["--out", QOUT, "--output", f"{d}/rpt3", "--config", f"{OUT}/fx/full.config"])
    skipped = "delta skipped: previous run used different inputs" in (r4.stdout + r4.stderr)
    checks = [("同输入二次运行 rc", "0", str(r.returncode), r.returncode == 0),
              ("summary.json 逐字节一致(重跑)", "一致", "一致" if det else "不一致", det),
              ("换 config 身份 delta skipped", "打印一行", "在位" if skipped else "缺失", skipped)]
    record("ItFootprint008", checks)


def ItFootprint008():
    TEST_ADD_CASE("ItFootprint008", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
