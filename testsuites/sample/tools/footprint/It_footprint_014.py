#!/usr/bin/env python3
"""ItFootprint014: ws63 平台与双工具链发现测试 (rpt_path 为 ItFootprint023 的比对输入)."""
from os_test import *

def TestCase():
    d = mk("ItFootprint014")
    r1 = run(["--out", WOUT, "--output", f"{d}/rpt_path"],
             env={"PATH": RVPATH + ":" + os.environ["PATH"]})
    ok1 = r1.returncode == 0 and "values measured" in r1.stdout
    r2 = run(["--out", WOUT, "--output", f"{d}/rpt_srctree"], env={"PATH": "/nonexistent"})
    ok2 = r2.returncode == 0 and "values measured" in r2.stdout
    j1 = json.load(open(f"{d}/rpt_path/summary.json"))["totals"]
    j2 = json.load(open(f"{d}/rpt_srctree/summary.json"))["totals"]
    same = all(j1[k] == j2[k] for k in ("ram_static", "stack", "dyn", "overhead", "ram_total"))
    exp = {"ram_static": 14359, "stack": 3072, "dyn": 10576, "overhead": 256}
    act = {k: j1.get(k) for k in exp}
    exact = act["stack"] == exp["stack"] and act["overhead"] == exp["overhead"]
    close = all(abs((act[k] or 0) - exp[k]) <= 300 for k in exp)
    checks = [("PATH 发现 rc+probe", "rc=0 probe 完成", f"rc={r1.returncode}", ok1),
              ("源树发现 rc+probe(空 PATH)", "rc=0 probe 完成", f"rc={r2.returncode}", ok2),
              ("两路径 totals 一致", "一致", "一致" if same else "不一致", same),
              ("基线值(stack/overhead 精确, static/dyn 树演进±300 内)", str(exp), str(act), exact and close)]
    open(f"{d}/stdout_path.txt", "w").write(r1.stdout)
    open(f"{d}/stdout_srctree.txt", "w").write(r2.stdout)
    record("ItFootprint014", checks)


def ItFootprint014():
    TEST_ADD_CASE("ItFootprint014", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
