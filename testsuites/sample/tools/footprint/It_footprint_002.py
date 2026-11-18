#!/usr/bin/env python3
"""ItFootprint002: 无工具链 fail-closed 测试 (含缺陷 F0001)."""
from os_test import *

def TestCase():
    d = mk("ItFootprint002")
    r = run(["--map", f"{OUT}/fx/plain/OHOS_Image.map", "--config", f"{OUT}/fx/full.config"],
            env={"PATH": "/nonexistent"})
    ok1 = r.returncode == 1 and "toolchain not found" in (r.stdout + r.stderr)
    checks = [("空 PATH --map+--config", "rc=1 且报 toolchain not found(含 --cc 出路)",
               f"rc={r.returncode}", ok1)]
    r2 = run(["--out", QOUT, "--cc", "/nonexistent_cc", "--output", f"{d}/r2"])
    ok2 = r2.returncode == 1 and "Traceback" in r2.stderr and "FileNotFoundError" in r2.stderr
    checks.append(("--cc 不存在文件(缺陷 F0001)", "rc=1 FileNotFoundError 裸栈", f"rc={r2.returncode}", ok2))
    open(f"{d}/raw.txt", "w").write(r.stdout + r.stderr + "\n=====\n" + r2.stdout + r2.stderr)
    record("ItFootprint002", checks)


def ItFootprint002():
    TEST_ADD_CASE("ItFootprint002", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
