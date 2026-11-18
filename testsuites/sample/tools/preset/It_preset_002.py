#!/usr/bin/env python3
"""ItPreset002: savemenuconfig 三挡位端到端测试 (preset_全量用例.md 1002/FT-01).

产物 itp002/<板>_<挡位>.config 供板级阶段安装为 vendor debug.config.
"""
from os_test import *

def TestCase():
    d = mkcase("itp002")
    checks = []
    for board, vendor in (("qemu", QEMU_VENDOR), ("ws63", WS63_VENDOR)):
        for tier in ("tiny", "small", "standard"):
            wf = f"{d}/{board}_{tier}.config"
            shutil.copy(vendor, wf)
            with open(wf, "a") as f:
                f.write("LOSCFG_KERNEL_RAM_PRESET=y\n")
                f.write(f"LOSCFG_KERNEL_RAM_PRESET_{tier.upper()}=y\n")
            rc, out = pty_make("savemenuconfig", os.path.abspath(wf))
            k = kload(wf)
            member = f"KERNEL_RAM_PRESET_{tier.upper()}"
            pv, mv = tri(k, "KERNEL_RAM_PRESET"), tri(k, member)
            txt = open(wf).read()
            no_managed_lines = ("BASE_CORE_TSK_LIMIT=" not in txt) and ("BASE_CORE_SWTMR_LIMIT=" not in txt)
            ok = rc == 0 and pv == 2 and mv == 2 and no_managed_lines
            checks.append((f"{board}_{tier} savemenuconfig", "rc=0,PRESET=2,成员=2,managed行不写回",
                           f"rc={rc},PRESET={pv},{member}={mv},managed行{'无' if no_managed_lines else '存在'}", ok))
    record("ItPreset002", checks)


def ItPreset002():
    TEST_ADD_CASE("ItPreset002", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
