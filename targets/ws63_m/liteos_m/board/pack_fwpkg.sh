#!/bin/bash
# ============================================================================
# @brief    Pack hb-built OHOS_Image.bin into a WS63 fwpkg
#           Standalone script — does not depend on GN/hb environment.
#
# Usage:    pack_fwpkg_new.sh <input_bin> <output_dir>
#   input_bin  : path to OHOS_Image.bin (from hb build)
#   output_dir : directory to output liteos_all.fwpkg
#
# Prerequisites (one-time):
#   NV bins must exist at $SDK/output/ws63/acore/nv_bin/ws63_all_nv*.bin
#   Generate via:
#     cd $SDK && prebuilts/python/linux-x86/current/bin/python3 \
#        build/config/target_config/ws63/build_nvbin.py ws63-liteos-app
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# readlink -m (not realpath): the liteos_m symlink shifts the relative base
# from device/ to kernel/, so anchor SDK via CODE_ROOT instead.
CODE_ROOT="$(readlink -m "$SCRIPT_DIR/../../../../../../")"
SDK="$CODE_ROOT/device/soc/hisilicon/ws63_m/sdk"

SIGN_CFG_DIR="$SDK/build/config/target_config/ws63/sign_config"
SIGN_TOOL="$SDK/tools/bin/sign_tool/sign_tool_pltuni"
BOOT_BIN_DIR="$SDK/output/ws63/acore/boot_bin"
PARAM_BIN_DIR="$SDK/output/ws63/acore/param_bin"
NV_BIN_DIR="$SDK/output/ws63/acore/nv_bin"

# Use prebuilts python3 (system python3 may lack pycparser for packet_create)
PYTHON3="${PYTHON3:-$CODE_ROOT/prebuilts/python/linux-x86/current/bin/python3}"
if [ ! -x "$PYTHON3" ]; then
    PYTHON3="python3"
fi

INPUT_BIN="${1:?Usage: pack_fwpkg_new.sh <input_bin> <output_dir>}"
OUT_DIR="${2:?Usage: pack_fwpkg_new.sh <input_bin> <output_dir>}"
WORK_DIR="$OUT_DIR/fwpkg_work"
mkdir -p "$WORK_DIR"

echo "============================================"
echo "  OHOS fwpkg packing"
echo "  Input:  $INPUT_BIN"
echo "  Output: $OUT_DIR/liteos_all.fwpkg"
echo "  Python: $PYTHON3"
echo "============================================"

# Sanity checks
for f in "$SIGN_TOOL" \
         "$BOOT_BIN_DIR/root_loaderboot_sign.bin" \
         "$BOOT_BIN_DIR/ssb_sign.bin" \
         "$BOOT_BIN_DIR/flashboot_sign.bin" \
         "$BOOT_BIN_DIR/flashboot_backup_sign.bin" \
         "$PARAM_BIN_DIR/root_params_sign.bin" \
         "$NV_BIN_DIR/ws63_all_nv.bin" \
         "$NV_BIN_DIR/ws63_all_nv_backup.bin" \
         "$SDK/tools/pkg/packet_create.py"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: missing $f"
        echo "NV bins can be generated via:"
        echo "  cd $SDK && $PYTHON3 build/config/target_config/ws63/build_nvbin.py ws63-liteos-app"
        exit 1
    fi
done

# Step 1: Copy and pad to 64-byte alignment (sign_tool requirement)
cp "$INPUT_BIN" "$WORK_DIR/liteos.bin"
"$PYTHON3" -c "
import os
f='$WORK_DIR/liteos.bin'
size=os.path.getsize(f)
if size % 64 != 0:
    pad = 64 - (size % 64)
    with open(f, 'ab') as fh:
        fh.write(b'\x00' * pad)
    print(f'padded {pad} bytes (was {size}, now {size+pad})')
else:
    print(f'already aligned ({size} bytes)')
"

# Step 2: Generate sign config (same template as pack_fwpkg.sh)
cat > "$WORK_DIR/liteos_sign.cfg" << EOF
SignSuite=4
SrcFile=$WORK_DIR/liteos.bin
DstFile=$WORK_DIR/liteos-sign.bin
RootKeyFile=$SIGN_CFG_DIR/ec_bp256_flash_private_key.pem
SubKeyFile=$SIGN_CFG_DIR/ec_bp256_app_private_key.pem
ImageId=0x4B0F2D1E
CodeInfoImageId=0x4B0F2D2D
KeyOwnerId=1
KeyId=1
KeyAlg=0x2A13C812
KeyVersion=0x00000000
KeyVersionMask=0x00000000
Msid=0x00000000
MsidMask=0x00000000
CompressFlag=0
Version=0x00000000
VersionMask=0x00000000
ProtectionKeyL1=00112233445566778899AABBCCDDEEFF
ProtectionKeyL2=00112233445566778899AABB00000000
PlainKey=8ABDA082DB74753577FF2D1E7D79DAC7
PlainKeyAuth=04040404040404040404040404040404
TextSegmentSize=0x00010000
RamSize=0x00010000
EOF

# Step 3: Sign (with unsigned fallback for dev boards)
echo "Signing liteos.bin..."
"$SIGN_TOOL" 0 "$WORK_DIR/liteos_sign.cfg" 2>&1 || {
    echo "WARN: sign_tool failed (likely missing .pem keys), using unsigned binary"
    cp "$WORK_DIR/liteos.bin" "$WORK_DIR/liteos-sign.bin"
}
ls -lh "$WORK_DIR/liteos-sign.bin" 2>/dev/null && echo "Sign done" || { echo "ERROR: sign step produced no output"; exit 1; }

# Step 4: Pack fwpkg via packet_create.packet_bin()
echo "Packing fwpkg..."
"$PYTHON3" << PYEOF
import sys, os
sys.path.insert(0, "$SDK/tools/pkg")
from packet_create import packet_bin

def fsize(f):
    return os.path.getsize(f)

# bash-expanded paths assigned to Python vars (avoid f-string {VAR} clash)
app = "$WORK_DIR/liteos-sign.bin"
boot = "$BOOT_BIN_DIR"
param = "$PARAM_BIN_DIR"
nv = "$NV_BIN_DIR"
outdir = "$OUT_DIR"
app_size = fsize(app)

entries = [
    f"{boot}/root_loaderboot_sign.bin|0x0|0x200000|0",
    f"{param}/root_params_sign.bin|0x200000|{hex(fsize(f'{param}/root_params_sign.bin'))}|1",
    f"{boot}/ssb_sign.bin|0x202000|{hex(fsize(f'{boot}/ssb_sign.bin'))}|1",
    f"{boot}/flashboot_sign.bin|0x220000|{hex(fsize(f'{boot}/flashboot_sign.bin'))}|1",
    f"{boot}/flashboot_backup_sign.bin|0x210000|{hex(fsize(f'{boot}/flashboot_backup_sign.bin'))}|1",
    f"{nv}/ws63_all_nv.bin|0x5fc000|{hex(fsize(f'{nv}/ws63_all_nv.bin'))}|1",
    f"{nv}/ws63_all_nv_backup.bin|0x20c000|{hex(fsize(f'{nv}/ws63_all_nv_backup.bin'))}|1",
    f"{app}|0x230000|{hex(app_size)}|1",
]

output = f"{outdir}/liteos_all.fwpkg"
packet_bin(output, entries)
print(f"fwpkg created: {output}")
print(f"fwpkg size: {fsize(output)} bytes")
PYEOF

# Cleanup intermediate files (keep fwpkg + sign cfg for debugging)
rm -rf "$WORK_DIR/liteos.bin" "$WORK_DIR/liteos-sign.bin"

echo ""
echo "============================================"
echo "  fwpkg done"
echo "  File: $OUT_DIR/liteos_all.fwpkg"
ls -lh "$OUT_DIR/liteos_all.fwpkg" 2>/dev/null
echo "============================================"
echo ""
echo "  Flash liteos_all.fwpkg with BurnTool"
echo "  APP partition address: 0x230000"
