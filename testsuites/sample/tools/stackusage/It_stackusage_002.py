#!/usr/bin/env python3
"""ItStackusage002: ARM 合成锚点族测试 (W2, D1/D2/D3/D9/帧计算)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage002")
    r = run(["--asm", f"{FX}/arm_syn.asm", "-j"])
    ok_rc = r.returncode == 0
    j = load_json(d) if ok_rc else {}
    m = funcs(j)
    exp = {"FuncA": (36, 52, False), "FuncB": (16, 16, False), "FuncC": (20, 100, False),
           "FuncD": (100, 100, False), "FuncE": (40, 40, False), "FuncF": (4, 4, False),
           "FuncR": (12, 12, True), "FuncG": (8, 8, False), "FuncH": (4, 4, False)}
    checks = [("rc=0", "0", str(r.returncode), ok_rc),
              ("架构识别", "arm", "arm" if "Architecture: arm" in r.stderr else "其他",
               "Architecture: arm" in r.stderr)]
    for n_, (lc, mc, rec) in exp.items():
        got = (m[n_][0]["LocalCost"], m[n_][0]["MaxCost"], m[n_][0]["IsRec"]) if n_ in m else None
        checks.append((n_, f"{lc}/{mc}/IsRec={rec}", f"{got}", got == (lc, mc, rec)))
    dup = sorted(e["MaxCost"] for e in m.get("dup", []))
    checks.append(("dup 双实例", "[8, 24]", str(dup), dup == [8, 24]))
    checks.append(("JSON 10 字段", "10", str(len(j["0x100"]) if j else 0),
                   bool(j) and sorted(j["0x100"]) == sorted(
                       ["FuncName", "FuncAddr", "LocalCost", "MaxCost", "Called",
                        "Location", "IsCalled", "IsRec", "Depth", "PtrVarName"])))
    checks.append(("Miss_funcptr 告警", "2 站点", f"{r.stdout.count('Miss_funcptr')}",
                   r.stdout.count("Miss_funcptr") == 2))
    checks.append(("环路径告警", "M->N->M", "在" if "<FuncM> -> <FuncN> -> <FuncM>" in r.stdout else "无",
                   "<FuncM> -> <FuncN> -> <FuncM>" in r.stdout))
    checks.append(("统计 total=16", "16", r.stdout.strip().splitlines()[-1] if r.stdout else "",
                   "total num = 16" in r.stdout))
    open(f"{d}/raw.txt", "w").write(r.stdout + r.stderr)
    record("ItStackusage002", checks)


def ItStackusage002():
    TEST_ADD_CASE("ItStackusage002", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
