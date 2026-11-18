#!/usr/bin/env python3
"""ItStackusage014: -L/-l 调用树过滤测试 (W10+补充 U7, 合成树+真实镜像)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage014")
    def nodes(extra):
        r = run(["--asm", f"{FX}/arm_tree.asm", "-sp", "-t", "TreeRoot", "-out"] + extra)
        return len([ln for ln in r.stdout.splitlines() if "symbol+--<" in ln])
    base, l60, l100, f13, f100 = nodes([]), nodes(["-L", "60"]), nodes(["-L", "100"]), \
        nodes(["-l", "13"]), nodes(["-l", "100"])
    checks = [("基线树 3 节点", "3", str(base), base == 3),
              ("-L 60 仅 max>=60 子节点", "2", str(l60), l60 == 2),
              ("-L 100 根保留", "1", str(l100), l100 == 1),
              ("-l 13 仅 local>=13 子节点", "2", str(f13), f13 == 2),
              ("-l 100 根保留", "1", str(f100), f100 == 1)]
    if have_real_images():
        r = run(["--asm", QASM, "-sp", "-t", "OsIdleTask", "-out"])
        b = len([ln for ln in r.stdout.splitlines() if "symbol+--<" in ln])
        r2 = run(["--asm", QASM, "-sp", "-t", "OsIdleTask", "-out", "-L", "512"])
        a = len([ln for ln in r2.stdout.splitlines() if "symbol+--<" in ln])
        checks.append(("真实镜像 -L 512 节点收敛", f"<{b}", f"{a} (基线 {b})", 0 < a < b))
    record("ItStackusage014", checks)


def ItStackusage014():
    TEST_ADD_CASE("ItStackusage014", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
