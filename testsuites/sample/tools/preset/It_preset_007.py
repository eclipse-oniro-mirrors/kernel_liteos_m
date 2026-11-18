#!/usr/bin/env python3
"""ItPreset007: vendor 显式值构建期生效测试 (preset_全量用例.md 1005/FT-05, D10 显式值覆盖挡位 default)."""
from os_test import *

def TestCase():
    d = mkcase("ItPreset007")
    wc = f"{OUT}/itp007.config"
    shutil.copy(f"{OUT}/itp002/qemu_standard.config", wc)
    with open(wc, "a") as f:
        f.write("LOSCFG_BASE_CORE_SWTMR_LIMIT=10\n")
    e2 = dict(os.environ); e2.update(KENV); e2["KCONFIG_CONFIG"] = wc
    r = subprocess.run(["genconfig", "--config-out", f"{OUT}/itp007_gen.config", "--header-path", f"{OUT}/itp007_config.h"],
                       cwd=KLM, env=e2, capture_output=True, text=True)
    gc = open(f"{OUT}/itp007_gen.config").read()
    gh = open(f"{OUT}/itp007_config.h").read()
    g10 = "LOSCFG_BASE_CORE_SWTMR_LIMIT=10" in gc
    h10 = re.search(r"define LOSCFG_BASE_CORE_SWTMR_LIMIT 10\b", gh) is not None
    rc, out = pty_make("savemenuconfig", os.path.abspath(wc))
    k = kload(wc)
    lim = ival(k, "BASE_CORE_SWTMR_LIMIT")
    txt = open(wc).read()
    checks = [("genconfig 显式值入 config", "SWTMR_LIMIT=10", "10" if g10 else "其他", g10),
              ("genconfig 显式值入 header", "=10", "10" if h10 else "其他", h10),
              ("savemenuconfig 复跑", "rc=0", f"rc={rc}", rc == 0),
              ("managed 清空显式行回落 16", "回落 16 且无显式行", f"值={lim},显式行{'无' if 'SWTMR_LIMIT=' not in txt else '有'}",
               lim == "16" and "LOSCFG_BASE_CORE_SWTMR_LIMIT=" not in txt)]
    record("ItPreset007", checks)


def ItPreset007():
    TEST_ADD_CASE("ItPreset007", TestCase, TEST_LOS, TEST_FN_BUILDTOOL_RAMPRESET, TEST_LEVEL0, TEST_FUNCTION)
