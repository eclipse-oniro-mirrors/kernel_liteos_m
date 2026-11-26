#!/usr/bin/env python3
"""ItFootprint009: CLI 负面族测试."""
from os_test import *

def TestCase():
    d = mk("ItFootprint009")
    fx = f"{OUT}/fx"
    cases = [
        ("无必选参数", [], 2, None),
        ("--out 与 --map 同给", ["--out", QOUT, "--map", f"{fx}/plain/OHOS_Image.map"], 2, None),
        ("--map 未配 --config", ["--map", f"{fx}/plain/OHOS_Image.map"], 1, "--map requires --config"),
        ("--out 不存在目录", ["--out", "/nonexistent_dir"], 1, "not a directory"),
        ("--out 无 map 目录", ["--out", f"{fx}/empty"], 1, "no *.map found"),
        ("--out 多 map 目录", ["--out", f"{fx}/twomaps", "--config", f"{fx}/full.config"], 1, "multiple maps"),
        ("--out 有 map 无 config", ["--out", f"{fx}/noconfig", "--cc", ARMCC], 1, "no config found"),
        ("--map 文件不存在", ["--map", "/nonexistent.map", "--config", f"{fx}/full.config"], 1, "map not found"),
        ("--out+--config 文件不存在", ["--out", QOUT, "--config", "/nonexistent.config"], 1, "config not found"),
    ]
    checks = []
    for name, args, expc, msg in cases:
        r = run(args, env={"PATH": os.environ["PATH"]}, timeout=120)
        out = r.stdout + r.stderr
        ok = r.returncode == expc and (msg is None or msg in out)
        checks.append((name, f"rc={expc}" + (f" 报 {msg}" if msg else ""),
                       f"rc={r.returncode}", ok))
    record("ItFootprint009", checks)


def ItFootprint009():
    TEST_ADD_CASE("ItFootprint009", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
