#!/usr/bin/env python3
"""ItStackusage016: ws63 真实镜像测试 (W12, RISC-V, 数值以当前基线为准)."""
from os_test import *

def TestCase():
    if not have_real_images():
        return skip("ItStackusage016", "缺 artifacts/ws63_baseline(先跑板级阶段)")
    d = mk("ItStackusage016")
    r = run(["--asm", WASM, "-j"])
    j = load_json(d) if r.returncode == 0 else {}
    top = max((v for k, v in j.items() if k != "Interrupt"),
              key=lambda v: v["MaxCost"], default={})
    checks = [("rc=0", "0", str(r.returncode), r.returncode == 0),
              ("架构识别 riscv", "riscv", "riscv" if "Architecture: riscv" in r.stderr else "其他",
               "Architecture: riscv" in r.stderr),
              ("统计 total=5459", "5459", "在" if "total num = 5459" in r.stdout else "无",
               "total num = 5459" in r.stdout),
              ("TOP1 testOsMemMemcpy002 Max=6512", "testOsMemMemcpy002/6512",
               f"{top.get('FuncName')}/{top.get('MaxCost')}",
               top.get("FuncName") == "testOsMemMemcpy002" and top.get("MaxCost") == 6512)]
    record("ItStackusage016", checks)


def ItStackusage016():
    TEST_ADD_CASE("ItStackusage016", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
