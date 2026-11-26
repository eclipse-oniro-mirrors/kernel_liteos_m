#!/usr/bin/env python3
"""ItPreset009: los_config.h 兜底默认静态测试 (preset_全量用例.md 1007/FT-07, D9)."""
from os_test import *

def TestCase():
    h = open(f"{KLM}/kernel/include/los_config.h").read()
    checks = []
    for sym in ("LOSCFG_BASE_IPC_SEM", "LOSCFG_BASE_IPC_MUX", "LOSCFG_BASE_IPC_QUEUE", "LOSCFG_BASE_CORE_SWTMR"):
        pat = re.search(rf"#ifndef {sym}[ \t]*\n[^\n]*#define {sym}[ \t]+(\d+)", h)
        v = pat.group(1) if pat else "无兜底"
        checks.append((f"{sym} 兜底", "0", v, v == "0"))
    record("ItPreset009", checks)


def ItPreset009():
    TEST_ADD_CASE("ItPreset009", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
