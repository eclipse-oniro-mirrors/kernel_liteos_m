#!/usr/bin/env python3
"""ItStackusage006: -irq 中断栈汇总测试 (W6, 总值=entry+max(ISR))."""
from os_test import *

def TestCase():
    d = mk("ItStackusage006")
    open(f"{d}/my.ini", "w").write("Interrupt_entry: ExcEntry\nInterrupt_isr: 5: IsrOne\n")
    r = run(["--asm", f"{FX}/arm_syn.asm", "-irq", "-j", "--config", f"{d}/my.ini"])
    got = load_json(d).get("Interrupt") if r.returncode == 0 else None
    exp = {"TotalStackCost": 130, "EntryCost": 120, "IsrCosts": {"5": 10}}
    checks = [("rc=0", "0", str(r.returncode), r.returncode == 0),
              ("Interrupt 汇总契约", str(exp), str(got), got == exp)]
    record("ItStackusage006", checks)


def ItStackusage006():
    TEST_ADD_CASE("ItStackusage006", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
