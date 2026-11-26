#!/usr/bin/env python3
"""ItFootprint016: R10 零依赖审计测试 (import 白名单=标准库+src 四模块)."""
from os_test import *

def TestCase():
    d = mk("ItFootprint016")
    allowed_std = set(getattr(sys, "stdlib_module_names", ())) | {"operator"}
    allowed_local = {"src", "input", "anchors", "scope", "output"}
    bad = []
    for root, _, fs in os.walk(FP):
        if "__pycache__" in root:
            continue
        for fn in fs:
            if not fn.endswith(".py"):
                continue
            tree = ast.parse(open(os.path.join(root, fn)).read())
            for node in ast.walk(tree):
                if isinstance(node, ast.Import):
                    for a in node.names:
                        mod = a.name.split(".")[0]
                        if mod not in allowed_std and mod not in allowed_local:
                            bad.append(f"{fn}:{mod}")
                elif isinstance(node, ast.ImportFrom) and node.module:
                    mod = node.module.split(".")[0]
                    if mod not in allowed_std and mod not in allowed_local:
                        bad.append(f"{fn}:{mod}")
    checks = [("import 白名单(标准库+src 四模块)", "无第三方依赖", "违规: " + str(bad) if bad else "无第三方依赖", not bad)]
    record("ItFootprint016", checks)


def ItFootprint016():
    TEST_ADD_CASE("ItFootprint016", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
