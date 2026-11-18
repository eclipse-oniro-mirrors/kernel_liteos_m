#!/usr/bin/env python3
"""ItFootprint015: --out 模式 config 发现链四分支测试 (F1~F4)."""
from os_test import *

def TestCase():
    d = mk("ItFootprint015")
    fx = f"{OUT}/fx"
    inj = f"{OUT}/it010/sizeof_inject.json"
    r1 = run(["--out", QOUT, "--config", f"{fx}/full.config", "--sizeof-table", inj, "--output", f"{d}/f1"],
             env={"PATH": "/nonexistent"})
    j1 = json.load(open(f"{d}/f1/summary.json"))["inputs"]
    ok1 = r1.returncode == 0 and "full.config" in j1.get("config", "") and "auto-converted" not in json.dumps(j1)
    d2 = f"{d}/topcfg"
    os.makedirs(d2, exist_ok=True)
    shutil.copy(f"{QOUT}/OHOS_Image.map", d2)
    shutil.copy(f"{QOUT}/toolchain.ninja", d2)
    shutil.copy(f"{fx}/full.config", f"{d2}/.config")
    r2 = run(["--out", d2, "--sizeof-table", inj, "--output", f"{d}/f2"], env={"PATH": "/nonexistent"})
    j2 = json.load(open(f"{d}/f2/summary.json"))["inputs"]
    ok2 = r2.returncode == 0 and ".config" in j2.get("config", "")
    d3 = f"{d}/subcfg/sub"
    os.makedirs(d3, exist_ok=True)
    shutil.copy(f"{QOUT}/OHOS_Image.map", d3)
    shutil.copy(f"{QOUT}/toolchain.ninja", d3)
    shutil.copy(f"{fx}/full.config", f"{d3}/.config")
    r3 = run(["--out", f"{d}/subcfg", "--sizeof-table", inj, "--output", f"{d}/f3"], env={"PATH": "/nonexistent"})
    j3 = json.load(open(f"{d}/f3/summary.json"))["inputs"]
    ok3 = r3.returncode == 0 and ".config" in j3.get("config", "")
    d4 = f"{d}/multicfg/a"
    d5 = f"{d}/multicfg/b"
    os.makedirs(d4, exist_ok=True)
    os.makedirs(d5, exist_ok=True)
    shutil.copy(f"{QOUT}/OHOS_Image.map", d4)
    shutil.copy(f"{QOUT}/toolchain.ninja", d4)
    shutil.copy(f"{fx}/full.config", f"{d4}/.config")
    shutil.copy(f"{fx}/full.config", f"{d5}/.config")
    r4 = run(["--out", f"{d}/multicfg", "--sizeof-table", inj, "--output", f"{d}/f4"], env={"PATH": "/nonexistent"})
    ok4 = r4.returncode == 1 and "multiple .config found" in (r4.stdout + r4.stderr)
    checks = [("F1 显式 --config", "命中且非 auto-converted", f"rc={r1.returncode}", ok1),
              ("F2 顶层 .config 优先", "命中", f"rc={r2.returncode}", ok2),
              ("F3 唯一递归 .config", "命中", f"rc={r3.returncode}", ok3),
              ("F4 多 .config", "rc=1 报 multiple .config found", f"rc={r4.returncode}", ok4)]
    record("ItFootprint015", checks)


def ItFootprint015():
    TEST_ADD_CASE("ItFootprint015", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
