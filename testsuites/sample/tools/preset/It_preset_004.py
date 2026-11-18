#!/usr/bin/env python3
"""ItPreset004: allnoconfig 平台保留测试 (preset_全量用例.md 1004/FT-03, D3 两步法)."""
from os_test import *

def TestCase():
    d = mkcase("ItPreset004")
    saved = f"{KLM}/.config"
    bak = None
    if os.path.exists(saved):
        bak = f"{OUT}/.config.pre_itp004.bak"; shutil.copy(saved, bak)
    checks = []
    for board, vendor in (("qemu", QEMU_VENDOR), ("ws63", WS63_VENDOR)):
        r = sh(["make", "-C", KLM, "allnoconfig", f"CONFIG={vendor}"])
        cfg = open(f"{KLM}/.config").read() if os.path.exists(f"{KLM}/.config") else ""
        plat = "LOSCFG_PLATFORM_QEMU_ARM_VIRT_CM4=y" if board == "qemu" else "LOSCFG_PLATFORM_WS63_M=y"
        checks.append((f"{board} allnoconfig rc", "0", str(r.returncode), r.returncode == 0))
        checks.append((f"{board} 平台保留", plat, plat if plat in cfg else "丢失", plat in cfg))
        for sym in ("LOSCFG_SHELL=y", "LOSCFG_FS_VFS=y", "LOSCFG_KERNEL_TEST=y"):
            checks.append((f"{board} 功能项 {sym} 关闭", "not set", sym if sym in cfg else "off", sym not in cfg))
    record("ItPreset004", checks)
    if bak:
        shutil.copy(bak, saved)


def ItPreset004():
    TEST_ADD_CASE("ItPreset004", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
