#!/usr/bin/env python3
"""ItStackusage010: CLI 退出码契约测试 (W9)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage010")
    a = f"{FX}/arm_syn.asm"
    cases = [("缺 --asm", [], 2), ("--arch foo", ["--asm", a, "--arch", "foo"], 2),
             ("-m 5", ["--asm", a, "-m", "5"], 2),
             ("-r 0", ["--asm", a, "-r", "0", "-j"], 1), ("-r -3", ["--asm", a, "-r", "-3", "-j"], 1)]
    checks = []
    for name, args, exp in cases:
        r = run(args)
        ok = r.returncode == exp
        if exp == 1:
            ok = ok and "rec-depth must be >= 1" in r.stderr
        checks.append((name, f"rc={exp}", f"rc={r.returncode}", ok))
    r = run(["--asm", a, "-t", "FuncA"])
    checks.append(("-t 无 -sp 全量模式", "rc=0", f"rc={r.returncode}", r.returncode == 0))
    record("ItStackusage010", checks)


def ItStackusage010():
    TEST_ADD_CASE("ItStackusage010", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
