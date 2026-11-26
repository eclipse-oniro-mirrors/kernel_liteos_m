#!/usr/bin/env python3
"""ItStackusage007: -irq 与 -sp/-t 互斥测试 (W6 互斥, D11)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage007")
    r1 = run(["--asm", f"{FX}/arm_syn.asm", "-irq", "-sp"])
    r2 = run(["--asm", f"{FX}/arm_syn.asm", "-irq", "-t", "FuncA"])
    checks = [("-irq -sp", "rc=1 且报互斥", f"rc={r1.returncode}",
               r1.returncode == 1 and "-irq 与 -sp/-t 互斥" in r1.stderr),
              ("-irq -t", "rc=1 且报互斥", f"rc={r2.returncode}",
               r2.returncode == 1 and "-irq 与 -sp/-t 互斥" in r2.stderr)]
    record("ItStackusage007", checks)


def ItStackusage007():
    TEST_ADD_CASE("ItStackusage007", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
