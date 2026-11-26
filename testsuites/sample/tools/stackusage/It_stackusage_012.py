#!/usr/bin/env python3
"""ItStackusage012: qemu 真实镜像单点峰值与调用树测试 (W10)."""
from os_test import *

def TestCase():
    if not have_real_images():
        return skip("ItStackusage012", "缺 artifacts/qemu_baseline(先跑板级阶段)")
    d = mk("ItStackusage012")
    r = run(["--asm", QASM, "-sp", "-t", "OsIdleTask", "-out"])
    row = next((ln for ln in r.stdout.splitlines() if ln.startswith("OsIdleTask ")), "")
    tree = [ln for ln in r.stdout.splitlines() if "symbol+--<" in ln]
    checks = [("rc=0", "0", str(r.returncode), r.returncode == 0),
              ("OsIdleTask 峰值=2904", "2904", row.split()[1] if row else "无行", "2904" in row),
              ("调用树 symbol+-- 缩进展开", ">0 节点", f"{len(tree)} 节点", len(tree) > 0),
              ("树含 *Rec/*Tail 标记", "在", "在" if "*Rec" in r.stdout else "无", "*Rec" in r.stdout),
              ("深度>15 截断(省略号)", "在", "在" if "..." in row else "无", "..." in row)]
    record("ItStackusage012", checks)


def ItStackusage012():
    TEST_ADD_CASE("ItStackusage012", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
