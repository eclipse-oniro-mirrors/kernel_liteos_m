#!/usr/bin/env python3
"""ItFootprint012: config 驱动门控矩阵测试 (D26 配置-二进制失配告警)."""
from os_test import *

def TestCase():
    d = mk("ItFootprint012")
    full = f"{OUT}/fx/full.config"
    inj = f"{OUT}/it010/sizeof_inject.json"
    r0 = run(["--map", f"{OUT}/fx/plain/OHOS_Image.map", "--config", full, "--sizeof-table", inj,
              "--output", f"{d}/base"], env={"PATH": "/nonexistent"})
    t0 = json.load(open(f"{d}/base/summary.json"))["totals"]["dyn"]
    variants = [("QUEUE 关", "LOSCFG_BASE_IPC_QUEUE", "n"), ("QUEUE_LIMIT 8", "LOSCFG_BASE_IPC_QUEUE_LIMIT", "8"),
                ("SEM_LIMIT 16", "LOSCFG_BASE_IPC_SEM_LIMIT", "16"), ("DEBUG_MUX 补齐", "LOSCFG_DEBUG_MUTEX", "y")]
    checks = [("基线 rc", "0", str(r0.returncode), r0.returncode == 0)]
    txt = open(full).read()
    for name, sym, val in variants:
        v = txt
        v = re.sub(rf"^({sym})=.*$", lambda m: f"{sym}={val}" if val != "n" else f"# {sym} is not set",
                   v, flags=re.M)
        if val == "n" and f"# {sym} is not set" not in v:
            v += f"\n# {sym} is not set\n"
        p = f"{d}/{sym}.config"
        open(p, "w").write(v)
        r = run(["--map", f"{OUT}/fx/plain/OHOS_Image.map", "--config", p, "--sizeof-table", inj,
                 "--output", f"{d}/{sym}"], env={"PATH": "/nonexistent"})
        t = json.load(open(f"{d}/{sym}/summary.json"))["totals"]["dyn"]
        if name == "DEBUG_MUX 补齐":
            rpt = open(f"{d}/{sym}/report.md").read()
            warn = ("告警" in rpt) or ("warn" in rpt.lower()) or ("失配" in rpt)
            checks.append((f"{name}(D26 配置-二进制失配)", "门过但 map 无伴生符号→按 0 计+告警",
                           f"{t0}→{t} (+{t-t0}), 告警{'在' if warn else '缺'}", t == t0 and warn))
        else:
            checks.append((f"{name} dyn 变化", "变化(出列/收缩)", f"{t0}→{t} ({t-t0:+d})", t != t0))
    record("ItFootprint012", checks)


def ItFootprint012():
    TEST_ADD_CASE("ItFootprint012", TestCase, TEST_LOS, TEST_FN_TESTTOOL_FOOTPRINT, TEST_LEVEL0, TEST_FUNCTION)
