#!/usr/bin/env python3
"""ItStackusage020: D3 混合环测试 (补充 U4, 不对称语义)."""
from os_test import *

def TestCase():
    d = mk("ItStackusage020")
    r = run(["--asm", f"{FX}/arm_syn2.asm", "-j"])
    m = funcs(load_json(d)) if r.returncode == 0 else {}
    p, q = m.get("FuncP", [{}])[0], m.get("FuncQ", [{}])[0]
    checks = [("FuncP Max=12(普通边叠加 Q 帧)", "12", str(p.get("MaxCost")), p.get("MaxCost") == 12),
              ("FuncP IsRec", "True", str(p.get("IsRec")), p.get("IsRec") is True),
              ("FuncQ Max=8(尾跳释放自身帧取 max)", "8", str(q.get("MaxCost")), q.get("MaxCost") == 8),
              ("FuncQ IsRec", "True", str(q.get("IsRec")), q.get("IsRec") is True),
              ("环路径告警 P->Q->P", "在", "在" if "<FuncP> -> <FuncQ> -> <FuncP>" in r.stdout else "无",
               "<FuncP> -> <FuncQ> -> <FuncP>" in r.stdout)]
    record("ItStackusage020", checks)


def ItStackusage020():
    TEST_ADD_CASE("ItStackusage020", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
