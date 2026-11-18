#!/usr/bin/env python3
"""ItStackusage009: RISC-V 架构与指令锚点测试 (W8, 自动识别+c.push+覆写)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage009")
    r = run(["--asm", f"{FX}/riscv_syn.asm", "-j"])
    m = funcs(load_json(d)) if r.returncode == 0 else {}
    r2 = run(["--asm", f"{FX}/riscv_syn.asm", "--arch", "arm"])
    checks = [("架构自动识别 riscv", "riscv", "riscv" if "Architecture: riscv" in r.stderr else "其他",
               "Architecture: riscv" in r.stderr),
              ("RvA Max=48(32+16)", "48", str(m.get("RvA", [{}])[0].get("MaxCost")),
               m.get("RvA", [{}])[0].get("MaxCost") == 48),
              ("RvTail 尾调用取 max=24", "24", str(m.get("RvTail", [{}])[0].get("MaxCost")),
               m.get("RvTail", [{}])[0].get("MaxCost") == 24),
              ("RvPush c.push Local=16", "16", str(m.get("RvPush", [{}])[0].get("LocalCost")),
               m.get("RvPush", [{}])[0].get("LocalCost") == 16),
              ("--arch arm 覆写 rc=0", "0", str(r2.returncode), r2.returncode == 0)]
    record("ItStackusage009", checks)


def ItStackusage009():
    TEST_ADD_CASE("ItStackusage009", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
