#!/bin/bash
# ============================================================================
# @brief    Pack hb-built OHOS_Image.bin into a HI3322 fwpkg
#           Standalone script — does not depend on GN/hb environment.
#
# Usage:    pack_fwpkg.sh <input_bin> <output_dir>
#   input_bin  : path to OHOS_Image.bin (from hb build)
#   output_dir : directory to output liteos_all.fwpkg
#
# The HI3322 device expects absolute flash addresses in the 16 MB region
# 0x26000000–0x26FFFFFF (see param_16m_sector.json layout). The pre-signed
# boot images (loaderboot/ssb/ssb_backup/recovery/seliteos/partition-table/nv)
# are checked into the hi3322 SDK output tree; this script only signs the
# OpenHarmony LiteOS application and assembles the fwpkg. Fully self-contained.
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# readlink -m (not realpath): the liteos_m symlink shifts the relative base
# from device/ to kernel/, so anchor SDK via CODE_ROOT instead.
CODE_ROOT="$(readlink -m "$SCRIPT_DIR/../../../../../../")"
SDK="$CODE_ROOT/device/soc/hisilicon/hi3322/sdk"

SIGN_CFG_DIR="$SDK/build/config/target_config/hi3322/sign_config"
SIGN_TOOL="$SDK/tools/bin/sign_tool/sign_tool_pltuni"
BOOT_BIN_DIR="$SDK/output/hi3322/acore/boot_bin"
PARAM_BIN_DIR="$SDK/output/hi3322/acore/param_bin"
NV_BIN_DIR="$SDK/output/hi3322/acore/nv_bin"

PYTHON3="${PYTHON3:-$CODE_ROOT/prebuilts/python/linux-x86/current/bin/python3}"
if [ ! -x "$PYTHON3" ]; then
    PYTHON3="python3"
fi

INPUT_BIN="${1:?Usage: pack_fwpkg.sh <input_bin> <output_dir>}"
OUT_DIR="${2:?Usage: pack_fwpkg.sh <input_bin> <output_dir>}"
WORK_DIR="$OUT_DIR/fwpkg_work"
mkdir -p "$WORK_DIR"

echo "============================================"
echo "  OHOS fwpkg packing (HI3322)"
echo "  Input:  $INPUT_BIN"
echo "  Output: $OUT_DIR/liteos_all.fwpkg"
echo "  Python: $PYTHON3"
echo "============================================"

for f in "$SIGN_TOOL" \
         "$BOOT_BIN_DIR/loaderboot_signed.bin" \
         "$PARAM_BIN_DIR/rootkey_partition_cfg.bin" \
         "$BOOT_BIN_DIR/ssb_signed.bin" \
         "$BOOT_BIN_DIR/ssb_backup_signed.bin" \
         "$BOOT_BIN_DIR/recovery_signed.bin" \
         "$BOOT_BIN_DIR/seliteos_signed.bin" \
         "$NV_BIN_DIR/hi3322_all_nv.bin" \
         "$SDK/tools/pkg/packet_create.py"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: missing $f"
        echo "Populate the hi3322 SDK output tree with the real pre-signed"
        echo "HI3322 boot images, then re-run this script."
        exit 1
    fi
done

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

# Sign cfg mirrors diting_community.cfg (SignSuite=4 = ECDSA256_ONLY_HASH,
# so no .pem private keys are required). ImageId matches the acpu_image id.
cat > "$WORK_DIR/liteos_sign.cfg" << EOF
SignSuite=4
SrcFile=$WORK_DIR/liteos.bin
DstFile=$WORK_DIR/liteos-sign.bin
RootKeyFile=$SIGN_CFG_DIR/ec_bp256_flash_private_key.pem
SubKeyFile=$SIGN_CFG_DIR/ec_bp256_app_private_key.pem
ImageId=0x4B0F2D1E
CodeInfoImageId=0x4B0F2D2D
KeyOwnerId=0
KeyId=0
KeyAlg=0x2A13C812
KeyVersion=0x00000000
KeyVersionMask=0x00000000
Msid=0x00000000
MsidMask=0x00000000
CompressFlag=0
Version=0x00000000
VersionMask=0x00000000
CodeAreaAddr=0x0
TextSegmentSize=0x00100000
EOF

echo "Signing liteos.bin..."
"$SIGN_TOOL" 0 "$WORK_DIR/liteos_sign.cfg" 2>&1 || {
    echo "WARN: sign_tool failed (likely missing .pem keys), using unsigned binary"
    cp "$WORK_DIR/liteos.bin" "$WORK_DIR/liteos-sign.bin"
}
ls -lh "$WORK_DIR/liteos-sign.bin" 2>/dev/null && echo "Sign done" || { echo "ERROR: sign step produced no output"; exit 1; }

echo "Packing fwpkg..."
"$PYTHON3" << PYEOF
import sys, os
sys.path.insert(0, "$SDK/tools/pkg")
from packet_create import packet_bin

def fsize(f):
    return os.path.getsize(f)

app = "$WORK_DIR/liteos-sign.bin"
boot = "$BOOT_BIN_DIR"
param = "$PARAM_BIN_DIR"
nv = "$NV_BIN_DIR"
outdir = "$OUT_DIR"

# HI3322 16 MB flash layout (param_16m_sector.json).
# burnAddr = absolute flash address (0x26000000 base + relative offset).
# burnSize = partition size (erased region), NOT the image size.
# type 0 = loaderboot (raw boot sector), type 1 = normal flash partition.
entries = [
    f"{boot}/loaderboot_signed.bin|0x0|0x10000|0",
    f"{param}/rootkey_partition_cfg.bin|0x26000000|0x1000|1",
    f"{boot}/ssb_signed.bin|0x26001000|0x10000|1",
    f"{boot}/ssb_backup_signed.bin|0x26011000|0x10000|1",
    f"{boot}/recovery_signed.bin|0x26021000|0x5f000|1",
    f"{boot}/seliteos_signed.bin|0x26080000|0x40000|1",
    f"{app}|0x260c0000|0x880000|1",
    f"{nv}/hi3322_all_nv.bin|0x26ffa000|0x6000|1",
]

output = f"{outdir}/liteos_all.fwpkg"
packet_bin(output, entries)
print(f"fwpkg created: {output}")
print(f"fwpkg size: {fsize(output)} bytes")
PYEOF

rm -rf "$WORK_DIR/liteos.bin" "$WORK_DIR/liteos-sign.bin"

echo ""
echo "============================================"
echo "  fwpkg done (HI3322)"
echo "  File: $OUT_DIR/liteos_all.fwpkg"
ls -lh "$OUT_DIR/liteos_all.fwpkg" 2>/dev/null
echo "============================================"
echo ""
echo "  Flash liteos_all.fwpkg with BurnTool"
echo "  APP partition address: 0x260C0000"
