#!/usr/bin/env python3
"""ItStackusage031: 覆盖率补充复测测试 (覆盖率矩阵, 需显式开启)."""
from os_test import *
import shutil

def TestCase():
    if os.environ.get("LITEOS_SU_COVERAGE") != "1" or not shutil.which("coverage"):
        return skip("ItStackusage031", "未设 LITEOS_SU_COVERAGE=1 或无 coverage.py(按需重放)")
    d = mk("ItStackusage031")
    cmds = [["--asm", f"{FX}/arm_syn.asm", "-j"],
            ["--asm", f"{FX}/arm_syn.asm", "-j", "-r", "3", "-sp", "-t", "FuncR,FuncM"],
            ["--asm", f"{FX}/arm_syn2.asm", "-j"],
            ["--asm", f"{FX}/riscv_syn.asm", "-j"],
            ["--asm", f"{FX}/riscv_syn2.asm", "-j"],
            ["--asm", f"{FX}/ring40.asm", "-j"],
            ["--asm", f"{FX}/arm_line.asm", "-m", "2", "-j"],
            ["--asm", f"{FX}/arm_tree.asm", "-sp", "-t", "TreeRoot", "-out", "-L", "60"]]
    ran = 0
    for c in cmds:
        if run(c).returncode == 0:
            ran += 1
    checks = [("覆盖率矩阵重放", f"0 失败/{len(cmds)} 条", f"{ran}/{len(cmds)} 通过", ran == len(cmds))]
    record("ItStackusage031", checks)


def ItStackusage031():
    TEST_ADD_CASE("ItStackusage031", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
