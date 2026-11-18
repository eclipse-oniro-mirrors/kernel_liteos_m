#!/usr/bin/env python3
"""ItStackusage022: RISC-V jalr 间接站点与 beq 目标调用测试 (补充 U6)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage022")
    r = run(["--asm", f"{FX}/riscv_syn2.asm", "-j"])
    m = funcs(load_json(d)) if r.returncode == 0 else {}
    checks = [("jalr a5 记为间接调用站点", "Miss_funcptr:<RvP>",
               "在" if "Miss_funcptr:<RvP>" in r.stdout else "无",
               "Miss_funcptr:<RvP>" in r.stdout),
              ("beq→函数起始按普通调用边 RvC Max=24", "24",
               str(m.get("RvC", [{}])[0].get("MaxCost")),
               m.get("RvC", [{}])[0].get("MaxCost") == 24)]
    record("ItStackusage022", checks)


def ItStackusage022():
    TEST_ADD_CASE("ItStackusage022", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
