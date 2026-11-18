#!/usr/bin/env python3
"""ItPreset006: apply_preset 多挡位双选防御测试 (preset_全量用例.md 2002/FT-04, 缺陷⑨ 死防御代码)."""
from os_test import *

def TestCase():
    d = mkcase("ItPreset006")
    dp = f"{OUT}/dual_preset.config"
    txt = open(QEMU_VENDOR).read()
    txt = re.sub(r"^LOSCFG_KERNEL_RAM_PRESET.*$", "", txt, flags=re.M)
    txt += "LOSCFG_KERNEL_RAM_PRESET=y\nLOSCFG_KERNEL_RAM_PRESET_TINY=y\nLOSCFG_KERNEL_RAM_PRESET_SMALL=y\n"
    open(dp, "w").write(txt)
    shutil.copy(dp, f"{KLM}/.config")
    e2 = {k: v for k, v in os.environ.items()}
    e2.update({"CONFIG_": "LOSCFG_", "srctree": KLM})
    r = subprocess.run(["python3", f"{KLM}/tools/apply_preset.py"], cwd=KLM, env=e2, capture_output=True, text=True)
    os.environ.update(KENV)
    from kconfiglib import Kconfig
    k = Kconfig(filename=f"{KLM}/Kconfig", warn=False); k.warn = False
    k.load_config(dp)
    k.syms["KERNEL_RAM_PRESET_TINY"].set_value(2)
    k.syms["KERNEL_RAM_PRESET_SMALL"].set_value(2)
    t, s = tri(k, "KERNEL_RAM_PRESET_TINY"), tri(k, "KERNEL_RAM_PRESET_SMALL")
    checks = [("双选 .config 走 apply_preset(缺陷⑨)", "rc=0 choice互斥收编", f"rc={r.returncode}", r.returncode == 0),
              ("API 双设复核", "压回单选", f"TINY={t},SMALL={s}", (t == 2) != (s == 2))]
    record("ItPreset006", checks)


def ItPreset006():
    TEST_ADD_CASE("ItPreset006", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
