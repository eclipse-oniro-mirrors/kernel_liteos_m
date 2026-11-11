#!/bin/bash
# Activate the stm32f407vet6_mini product without creating top-level
# vendor/<company>/ or device/<company>/ repositories:
#
#   kernel/liteos_m/targets/stm32f407vet6/liteos_m       board code (device_path)
#   kernel/liteos_m/vendor/st/stm32f407vet6_mini         product definition
#
# This script replaces the `hb set` selection step (hb set only scans the
# top-level vendor/ directory and cannot see this product) by writing
# out/ohos_config.json directly; `hb build` then works as usual.
#
# Usage:
#   bash kernel/liteos_m/vendor/st/stm32f407vet6_mini/switch_product.sh [ohos_root]
set -e

ROOT="${1:-$(cd "$(dirname "$0")/../../../../.." && pwd)}"
PRODUCT="$ROOT/kernel/liteos_m/vendor/st/stm32f407vet6_mini"
DEVICE="$ROOT/kernel/liteos_m/targets/stm32f407vet6/liteos_m"

if [ ! -f "$PRODUCT/config.json" ] || [ ! -e "$DEVICE/BUILD.gn" ]; then
    echo "error: product files not found under $ROOT (wrong ohos_root?)" >&2
    exit 1
fi

python3 - "$ROOT" "$PRODUCT" "$DEVICE" <<'PY'
import json
import os
import sys

root, product, device = sys.argv[1], sys.argv[2], sys.argv[3]
p = os.path.join(root, "out/ohos_config.json")
d = json.load(open(p)) if os.path.exists(p) else {}
d.update({
    "root_path": root,
    "board": "stm32f407vet6",
    "kernel": "liteos_m",
    "product": "stm32f407vet6_mini",
    "product_path": product,
    "device_path": device,
    "device_company": "st",
    "os_level": "mini",
    "version": "3.0",
    "patch_cache": None,
    "product_json": product + "/config.json",
    "component_type": "",
    "product_config_path": product,
    "target_cpu": None,
    "target_os": None,
    "out_path": root + "/out/stm32f407vet6/stm32f407vet6_mini",
    "compile_mode": "cross",
    "subsystem_config_json": "build/subsystem_config.json",
    "device_config_path": device,
    "support_cpu": None,
    "precise_branch": None,
    "compile_config": None,
    "log_mode": "normal",
})
os.makedirs(os.path.dirname(p), exist_ok=True)
json.dump(d, open(p, "w"), indent=2)
print("activated product stm32f407vet6_mini ->", p)
PY
