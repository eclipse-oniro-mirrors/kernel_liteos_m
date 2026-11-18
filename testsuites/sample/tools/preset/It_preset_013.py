#!/usr/bin/env python3
"""ItPreset013: TINY+显式 SWTMR=y 组合挡位默认测试 (preset_全量用例.md 1011/S1, D5)."""
from os_test import *

def TestCase():
    wc = f"{OUT}/itp013.config"
    txt = open(QEMU_VENDOR).read()
    txt = re.sub(r"^LOSCFG_KERNEL_RAM_PRESET.*$", "", txt, flags=re.M)
    txt = re.sub(r"^LOSCFG_BASE_CORE_SWTMR_LIMIT=.*$", "", txt, flags=re.M)
    txt += "LOSCFG_KERNEL_RAM_PRESET=y\nLOSCFG_KERNEL_RAM_PRESET_TINY=y\nLOSCFG_BASE_CORE_SWTMR=y\n"
    open(wc, "w").write(txt)
    k = kload(wc)
    isr, lim, swtmr = tri(k, "BASE_CORE_SWTMR_IN_ISR"), ival(k, "BASE_CORE_SWTMR_LIMIT"), tri(k, "BASE_CORE_SWTMR")
    checks = [("直评 SWTMR_IN_ISR", "2", str(isr), isr == 2),
              ("直评 SWTMR_LIMIT", "2", str(lim), lim == "2"),
              ("直评 SWTMR(显式 y)", "2", str(swtmr), swtmr == 2)]
    rc, _ = pty_make("savemenuconfig", os.path.abspath(wc))
    k2 = kload(wc)
    swtmr2 = tri(k2, "BASE_CORE_SWTMR")
    txt2 = open(wc).read()
    checks.append(("savemenuconfig 复跑", "rc=0", f"rc={rc}", rc == 0))
    checks.append(("显式 SWTMR=y 被清(挡位接管 n)", "SWTMR=n 且无显式行",
                   f"SWTMR={swtmr2},显式行{'无' if 'LOSCFG_BASE_CORE_SWTMR=y' not in txt2 else '有'}",
                   swtmr2 == 0 and "LOSCFG_BASE_CORE_SWTMR=y" not in txt2))
    record("ItPreset013", checks)


def ItPreset013():
    TEST_ADD_CASE("ItPreset013", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
