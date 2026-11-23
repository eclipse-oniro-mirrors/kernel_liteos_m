#!/usr/bin/env python3
"""ItPreset008: 板级与非 managed 符号保留测试 (preset_全量用例.md 1006/FT-06, D2 仅清 managed user_value)."""
from os_test import *

def TestCase():
    checks = []
    qs = open(f"{OUT}/itp002/qemu_standard.config").read()
    for sym in ("LOSCFG_PLATFORM_QEMU_ARM_VIRT_CM4=y", "LOSCFG_CORTEX_M_SYSTICK=y", "LOSCFG_CORTEX_M_NVIC=y",
                "LOSCFG_HWI_PRE_POST_PROCESS=y"):
        checks.append((f"qemu STD 保留 {sym}", "在位", "在位" if sym in qs else "丢失", sym in qs))
    checks.append(("qemu STD 剔除 LOSCFG_KERNEL_TEST(上游!1319挡位依赖)", "剔除",
                   "剔除" if "LOSCFG_KERNEL_TEST=y" not in qs else "在位", "LOSCFG_KERNEL_TEST=y" not in qs))
    ws = open(f"{OUT}/itp002/ws63_standard.config").read()
    for sym in ("LOSCFG_PLATFORM_WS63_M=y", "LOSCFG_DW_APB_TIMER=y", "LOSCFG_BASE_IPC_RWSEM=y"):
        checks.append((f"ws63 STD 保留 {sym}", "在位", "在位" if sym in ws else "丢失", sym in ws))
    checks.append(("ws63 STD 剔除 LOSCFG_KERNEL_TEST(上游!1319挡位依赖)", "剔除",
                   "剔除" if "LOSCFG_KERNEL_TEST=y" not in ws else "在位", "LOSCFG_KERNEL_TEST=y" not in ws))
    record("ItPreset008", checks)


def ItPreset008():
    TEST_ADD_CASE("ItPreset008", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
