#!/usr/bin/env python3
"""ItFootprint023: ws63 源树工具链发现分支测试 (V13b, 依赖 ItFootprint014 的 rpt_path)."""
from os_test import *

def TestCase():
    d = mk("ItFootprint023")
    r = run(["--out", WOUT, "--output", f"{d}/rpt"], env={"PATH": "/nonexistent"})
    ok = r.returncode == 0 and "values measured" in r.stdout
    j = json.load(open(f"{d}/rpt/summary.json"))["totals"]
    j14 = json.load(open(f"{OUT}/ItFootprint014/rpt_path/summary.json"))["totals"]
    same = all(j[k] == j14[k] for k in ("ram_static", "stack", "dyn", "overhead"))
    checks = [("空 PATH --out(源树发现)", "rc=0 且探针完成", f"rc={r.returncode}", ok),
              ("与 PATH 发现 totals 一致(V13b)", "一致", "一致" if same else "不一致", same)]
    record("ItFootprint023", checks)


def ItFootprint023():
    TEST_ADD_CASE("ItFootprint023", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
