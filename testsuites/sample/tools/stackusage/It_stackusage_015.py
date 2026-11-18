#!/usr/bin/env python3
"""ItStackusage015: qemu 真实镜像 -irq 中断栈测试 (W11, 数值以当前基线为准)."""
from os_test import *

def TestCase():
    if not have_real_images():
        return skip("ItStackusage015", "缺 artifacts/qemu_baseline(先跑板级阶段)")
    d = mk("ItStackusage015")
    open(f"{d}/my.ini", "w").write("Interrupt_entry: HalExcHandleEntry\nInterrupt_isr: 15: SysTick_Handler\n")
    r = run(["--asm", QASM, "-irq", "-j", "--config", f"{d}/my.ini"])
    got = load_json(d).get("Interrupt") if r.returncode == 0 else None
    exp = {"TotalStackCost": 3432, "EntryCost": 3424, "IsrCosts": {"15": 8}}
    checks = [("rc=0", "0", str(r.returncode), r.returncode == 0),
              ("Interrupt 汇总(总值=entry+max ISR)", str(exp), str(got), got == exp)]
    record("ItStackusage015", checks)


def ItStackusage015():
    TEST_ADD_CASE("ItStackusage015", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
