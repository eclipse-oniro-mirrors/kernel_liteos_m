#!/usr/bin/env python3
"""ItStackusage008: -m 1/2 源码定位测试 (W7; 告警格式为登记缺陷 S003 现状)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage008")
    r1 = run(["--asm", f"{FX}/arm_line.asm", "-m", "1", "-j"])
    j1 = load_json(d) if r1.returncode == 0 else {}
    shutil.rmtree(f"{d}/out", ignore_errors=True)
    r2 = run(["--asm", f"{FX}/arm_line.asm", "-m", "2", "-j"])
    j2 = load_json(d) if r2.returncode == 0 else {}
    checks = [("-m 1 rc=0", "0", str(r1.returncode), r1.returncode == 0),
              ("-m 1 告警带源码行(los_demo.c 11 空格分隔, S003 现状)", "在",
               "在" if "Miss_funcptr:<FuncG>:los_demo.c 11" in r1.stdout + r1.stderr else "无",
               "Miss_funcptr:<FuncG>:los_demo.c 11" in r1.stdout + r1.stderr),
              ("-m 1 JSON Location 启用", "los_demo.c:10",
               str(j1.get("0x480", {}).get("Location")),
               j1.get("0x480", {}).get("Location") == "los_demo.c:10"),
              ("-m 2 PtrVarName 命中", "['pfnTask']", str(j2.get("0x480", {}).get("PtrVarName")),
               j2.get("0x480", {}).get("PtrVarName") == ["pfnTask"]),
              ("-m 2 FuncB Location", "los_demo.c:20", str(j2.get("0x180", {}).get("Location")),
               j2.get("0x180", {}).get("Location") == "los_demo.c:20")]
    record("ItStackusage008", checks)


def ItStackusage008():
    TEST_ADD_CASE("ItStackusage008", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
