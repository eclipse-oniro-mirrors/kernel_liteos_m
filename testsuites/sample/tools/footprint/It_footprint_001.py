#!/usr/bin/env python3
"""ItFootprint001: footprint 基础设施与结构测试."""
from os_test import *

def TestCase():
    files = ["footprint.py", "src/input.py", "src/anchors.py", "src/scope.py", "src/output.py",
             "README.md", "README_zh.md"]
    checks = [("文件在位: " + f, "在位", "在位" if os.path.exists(f"{FP}/{f}") else "缺失",
               os.path.exists(f"{FP}/{f}")) for f in files]
    sys.path.insert(0, f"{FP}/src")
    os.environ["CONFIG_"] = "LOSCFG_"
    from anchors import DEFAULT_ANCHORS
    from scope import DEFAULT_SCOPE
    checks.append(("锚点表条数", "23", str(len(DEFAULT_ANCHORS)), len(DEFAULT_ANCHORS) == 23))
    checks.append(("组前缀数", "12", str(len(DEFAULT_SCOPE['group_prefixes'])), len(DEFAULT_SCOPE['group_prefixes']) == 12))
    record("ItFootprint001", checks)


def ItFootprint001():
    TEST_ADD_CASE("ItFootprint001", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
