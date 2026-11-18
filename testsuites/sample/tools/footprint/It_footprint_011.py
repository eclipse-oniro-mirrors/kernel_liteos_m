#!/usr/bin/env python3
"""ItFootprint011: --sizeof-table fail-closed 负面测试 (含缺陷 F0002)."""
from os_test import *

def TestCase():
    d = mk("ItFootprint011")
    fx = f"{OUT}/fx"
    cases = [
        ("JSON 数组", f"{fx}/st_array.json", "must be a JSON object"),
        ("字符串值", f"{fx}/st_str.json", "must be a JSON object"),
        ("文件缺失", "/nonexistent_st.json", "cannot load --sizeof-table"),
        ("值 0(缺陷 F0002)", f"{fx}/st_zero.json", "可通过(仅 isinstance 校验)"),
    ]
    checks = []
    for name, tbl, msg in cases:
        r = run(["--out", QOUT, "--sizeof-table", tbl, "--output", f"{d}/r"], env={"PATH": "/nonexistent"}, timeout=120)
        out = r.stdout + r.stderr
        if "F0002" in name:
            ok = r.returncode == 0
        else:
            ok = r.returncode == 1 and msg in out
        checks.append((name, msg, f"rc={r.returncode}", ok))
    record("ItFootprint011", checks)


def ItFootprint011():
    TEST_ADD_CASE("ItFootprint011", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
