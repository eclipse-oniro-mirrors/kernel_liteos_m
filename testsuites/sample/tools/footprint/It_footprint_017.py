#!/usr/bin/env python3
"""ItFootprint017: D14/D17 真实 map 实证测试."""
from os_test import *

def TestCase():
    d = mk("ItFootprint017")
    mp = f"{QOUT}/OHOS_Image.map"
    txt = open(mp, errors="replace").read()
    has_output_marker = "OUTPUT(" in txt
    pairs = re.findall(r"^\s*(0x[0-9a-f]+)\s+0x[0-9a-f]+\s+(0x[0-9a-f]+)\s+(\.\S+)$", txt, re.M)
    dup = len(pairs) - len(set(pairs))
    checks = [("OUTPUT( 终界标记存在(D17)", "在位", "在位" if has_output_marker else "缺失", has_output_marker),
              ("本构建无同(地址,段)重复记录(D14)", "0 组", f"{dup} 组", dup == 0)]
    open(f"{d}/scan.txt", "w").write(f"OUTPUT marker={has_output_marker}\nplaced records={len(pairs)}\ndup groups={dup}\n")
    record("ItFootprint017", checks)


def ItFootprint017():
    TEST_ADD_CASE("ItFootprint017", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
