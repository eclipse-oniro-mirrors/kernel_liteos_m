#!/usr/bin/env python3
"""ItPreset014: apply_preset 无挡位早退测试 (preset_全量用例.md 1012/S3)."""
from os_test import *

def TestCase():
    wc = f"{OUT}/itp014.config"
    txt = re.sub(r"^LOSCFG_KERNEL_RAM_PRESET.*$", "", open(QEMU_VENDOR).read(), flags=re.M)
    open(wc, "w").write(txt)
    m1 = md5(wc)
    shutil.copy(wc, f"{KLM}/.config")
    e2 = {k: v for k, v in os.environ.items()}; e2.update({"CONFIG_": "LOSCFG_", "srctree": KLM})
    r = subprocess.run(["python3", f"{KLM}/tools/apply_preset.py"], cwd=KLM, env=e2, capture_output=True, text=True)
    m2 = md5(f"{KLM}/.config")
    checks = [("apply_preset 无挡位早退", "rc=0", f"rc={r.returncode}", r.returncode == 0),
              (".config 逐字节未改", "md5 一致", f"{m1[:8]}=={m2[:8]}", m1 == m2)]
    record("ItPreset014", checks)


def ItPreset014():
    TEST_ADD_CASE("ItPreset014", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
