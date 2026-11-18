#!/usr/bin/env python3
"""ItStackusage013: 单点入口缺失容错测试 (W10+补充 U9)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage013")
    r1 = run(["--asm", f"{FX}/arm_syn.asm", "-sp", "-t", "NoSuchFuncXyz"])
    r2 = run(["--asm", f"{FX}/arm_syn.asm", "-sp", "-t", "FuncA,NoSuchXyz,FuncB", "-out"])
    checks = [("缺失入口 rc=0 继续分析", "0", str(r1.returncode), r1.returncode == 0),
              ("stderr 报 not found", "Entry function not found: NoSuchFuncXyz",
               "在" if "Entry function not found: NoSuchFuncXyz" in r1.stderr else "无",
               "Entry function not found: NoSuchFuncXyz" in r1.stderr),
              ("混合入口存在项正常出表", "FuncA/FuncB 两行",
               f"{sum(1 for ln in r2.stdout.splitlines() if ln.startswith(('FuncA ', 'FuncB ')))} 行",
               sum(1 for ln in r2.stdout.splitlines() if ln.startswith(("FuncA ", "FuncB "))) == 2)]
    record("ItStackusage013", checks)


def ItStackusage013():
    TEST_ADD_CASE("ItStackusage013", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
