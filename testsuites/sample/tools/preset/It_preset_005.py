#!/usr/bin/env python3
"""ItPreset005: allnoconfig/savemenuconfig 负面场景测试 (preset_全量用例.md 2001, FT-03 负面+S2+S4+S6)."""
from os_test import *

def TestCase():
    d = mkcase("ItPreset005")
    np = f"{OUT}/no_platform.config"
    txt = re.sub(r"^LOSCFG_PLATFORM_.*=y.*$", "", open(QEMU_VENDOR).read(), flags=re.M)
    open(np, "w").write(txt)
    r = sh(["make", "-C", KLM, "allnoconfig", f"CONFIG={np}"])
    e2 = {k: v for k, v in os.environ.items() if k != "KCONFIG_ALLCONFIG"}
    e2.update({"CONFIG_": "LOSCFG_", "srctree": KLM, "KCONFIG_ALLCONFIG": np})
    r_dir = subprocess.run(["python3", f"{KLM}/tools/allnoconfig.py"], cwd=KLM, env=e2, capture_output=True, text=True)
    checks = [("S2 无平台 config allnoconfig(make 包裹)", "报 no LOSCFG_PLATFORM_*=y found",
               f"rc={r.returncode}, msg={'有' if 'no LOSCFG_PLATFORM_' in r.stderr else '无'}",
               "no LOSCFG_PLATFORM_" in r.stderr),
              ("S2 直调 allnoconfig.py", "rc=1", f"rc={r_dir.returncode}", r_dir.returncode == 1)]
    r2 = sh(["make", "-C", KLM, "allnoconfig", "CONFIG=/nonexistent/x.config"])
    checks.append(("S4 不存在路径 allnoconfig", "报 no vendor config(make rc=2)",
                   f"rc={r2.returncode}, msg={'有' if 'no vendor config' in (r2.stdout + r2.stderr) else '无'}",
                   r2.returncode == 2 and "no vendor config" in (r2.stdout + r2.stderr)))
    r3 = sh(["make", "-C", KLM, "savemenuconfig", "CONFIG=/nonexistent/x.config"])
    silent = "no vendor config" not in (r3.stdout + r3.stderr)
    checks.append(("S4 不存在路径 savemenuconfig(缺陷⑩)", "静默短路: 报错但无文案",
                   f"rc={r3.returncode}, 文案={'无(静默)' if silent else '有'}",
                   r3.returncode == 2 and silent))
    e3 = {k: v for k, v in os.environ.items() if k != "KCONFIG_ALLCONFIG"}
    e3.update({"CONFIG_": "LOSCFG_", "srctree": KLM})
    r4 = subprocess.run(["python3", f"{KLM}/tools/allnoconfig.py"], cwd=KLM, env=e3, capture_output=True, text=True)
    checks.append(("S6 无 KCONFIG_ALLCONFIG", "rc=1 且报 KCONFIG_ALLCONFIG must point",
                   f"rc={r4.returncode}, err={r4.stderr.strip()[:60]}",
                   r4.returncode == 1 and "KCONFIG_ALLCONFIG must point" in r4.stderr))
    record("ItPreset005", checks)


def ItPreset005():
    TEST_ADD_CASE("ItPreset005", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
