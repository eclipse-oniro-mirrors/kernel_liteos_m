#!/usr/bin/env python3
"""ItStackusage011: qemu 真实镜像全量与确定性测试 (W10, 数值以当前基线为准)."""
from os_test import *
import hashlib

def TestCase():
    if not have_real_images():
        return skip("ItStackusage011", "缺 artifacts/qemu_baseline/OHOS_Image.asm(先跑板级阶段)")
    d = mk("ItStackusage011")
    r = run(["--asm", QASM, "-j"])
    ok_rc = r.returncode == 0
    j = load_json(d) if ok_rc else {}
    md5_1 = hashlib.md5(open(f"{d}/out/funcstack.json", "rb").read()).hexdigest()
    run(["--asm", QASM, "-j"])
    md5_2 = hashlib.md5(open(f"{d}/out/funcstack.json", "rb").read()).hexdigest()
    top = max((v for k, v in j.items() if k != "Interrupt"),
              key=lambda v: v["MaxCost"], default={})
    checks = [("rc=0", "0", str(r.returncode), ok_rc),
              ("统计行", "total num = 6477", "在" if "total num = 6477" in r.stdout else str(r.stdout.strip().splitlines()[-1] if r.stdout else ""),
               "total num = 6477" in r.stdout),
              ("uncalled", "3529", "在" if "uncalled function count = 3529" in r.stdout else "无",
               "uncalled function count = 3529" in r.stdout),
              ("TOP1 testOsMemMemcpy002 Max=6128", "testOsMemMemcpy002/6128",
               f"{top.get('FuncName')}/{top.get('MaxCost')}",
               top.get("FuncName") == "testOsMemMemcpy002" and top.get("MaxCost") == 6128),
              ("重跑逐字节一致 (D12)", "一致", "一致" if md5_1 == md5_2 else "漂移", md5_1 == md5_2)]
    open(f"{d}/raw.txt", "w").write(r.stdout + r.stderr)
    record("ItStackusage011", checks)


def ItStackusage011():
    TEST_ADD_CASE("ItStackusage011", TestCase, TEST_LOS, TEST_FN_TESTTOOL_STACKUSAGE, TEST_LEVEL0, TEST_FUNCTION)
