#!/usr/bin/env python3
"""Regenerate footprint replay inputs (fx/ fixtures + it010/sizeof_inject.json).

Run AFTER run_all_cases.sh board stage (needs artifacts/qemu_baseline).
- fx/plain/OHOS_Image.map  <- copy of archived qemu baseline map
- fx/full.config           <- kconfiglib full .config resolved from qemu vendor baseline
- fx/{twomaps,noconfig,empty}, st_{array,str,zero}.json
- it010/sizeof_inject.json <- probe values captured by monkey-patching
                              anchors.process_anchors during one real --out run
"""
import json, os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "../../../../../.."))
FP = f"{ROOT}/kernel/liteos_m/tools/footprint"
RUN = f"{ROOT}/testresult"
QOUT = f"{RUN}/artifacts/qemu_baseline"
QV = f"{ROOT}/vendor/ohemu/qemu_mini_system_demo/kernel_configs/debug.config"
OUT = f"{RUN}/footprint"

def die(m):
    print("FATAL:", m); sys.exit(1)

os.path.exists(f"{QOUT}/OHOS_Image.map") or die("缺少 qemu_baseline 产物(先跑 board 阶段)")
os.path.exists(f"{QOUT}/toolchain.ninja") or die("缺少 toolchain.ninja")

fx = f"{OUT}/fx"
shutil.rmtree(fx, ignore_errors=True)
os.makedirs(f"{fx}/plain"); os.makedirs(f"{fx}/twomaps"); os.makedirs(f"{fx}/noconfig"); os.makedirs(f"{fx}/empty")
shutil.copy(f"{QOUT}/OHOS_Image.map", f"{fx}/plain/OHOS_Image.map")
shutil.copy(f"{QOUT}/OHOS_Image.map", f"{fx}/noconfig/OHOS_Image.map")
open(f"{fx}/twomaps/a.map", "w").write("placeholder\n")
open(f"{fx}/twomaps/b.map", "w").write("placeholder\n")
open(f"{fx}/st_array.json", "w").write("[]\n")
open(f"{fx}/st_str.json", "w").write('{"sz:UINT32": "abc"}\n')
open(f"{fx}/st_zero.json", "w").write('{"sz:UINT32": 0}\n')

os.environ.update({"CONFIG_": "LOSCFG_", "srctree": f"{ROOT}/kernel/liteos_m"})
def config_from_header(hdr_path, out_path):
    lines = []
    for l in open(hdr_path):
        m = re.match(r"#define (LOSCFG_[A-Z0-9_]+) (\S+)", l)
        if not m:
            continue
        name, val = m.group(1), m.group(2)
        if val == "1":
            lines.append(f"{name[7:]}=y" if not name.startswith("LOSCFG_") else f"{name}=y")
        elif val == "0":
            continue
        else:
            lines.append(f"{name}={val}")
    open(out_path, "w").write("\n".join(lines) + "\n")
import re
config_from_header(f"{QOUT}/config.h", f"{fx}/full.config")
print("fx fixtures written (full.config converted from config.h)")

sys.path.insert(0, f"{FP}/src")
import anchors
cap = {}
orig = anchors.process_anchors
def wrap(*a, **kw):
    items, pv = orig(*a, **kw)
    cap.update(pv)
    return items, pv
anchors.process_anchors = wrap
import runpy
sys.argv = ["footprint.py", "--out", QOUT, "--output", f"{OUT}/_probe_tmp"]
try:
    runpy.run_path(f"{FP}/footprint.py", run_name="__main__")
except SystemExit as e:
    if e.code not in (0, None):
        die(f"probe run failed rc={e.code}")
if not cap:
    die("probe captured no values")
os.makedirs(f"{OUT}/it010", exist_ok=True)
json.dump(cap, open(f"{OUT}/it010/sizeof_inject.json", "w"), indent=1, sort_keys=True)
shutil.rmtree(f"{OUT}/_probe_tmp", ignore_errors=True)
print(f"sizeof_inject.json written ({len(cap)} values)")
