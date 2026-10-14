#!/bin/bash
# build_key_demo.sh - 编译板端测试程序 key_demo(静态链 libkeydriver.a)
# 用法: bash build_key_demo.sh [OH源码根目录]   (默认 /code/3516)
# 前置: 先跑过 build_keydriver.sh(需要本目录 libkeydriver.a)
# 产物: key_demo (push 到板子 /bin/ 后用 'exec /bin/key_demo ...' 调用)
set -e
cd "$(dirname "$0")"

REPO="${1:-/code/3516}"
OUT="$REPO/out/hispark_taurus/ipcamera_hispark_taurus"
CLANG="$REPO/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang"

FLAGS="--target=arm-liteos-eabi -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 -O2 -Wall"
SYS="--sysroot=$OUT/sysroot"
HDF_I="-I$REPO/drivers/hdf_core/framework/include/core \
       -I$REPO/drivers/hdf_core/framework/include/osal \
       -I$REPO/drivers/hdf_core/framework/include/platform \
       -I$REPO/drivers/hdf_core/framework/include/utils \
       -I$REPO/drivers/hdf_core/interfaces/inner_api/core \
       -I$REPO/drivers/hdf_core/interfaces/inner_api/osal/shared \
       -I$REPO/drivers/hdf_core/interfaces/inner_api/osal/uhdf \
       -I$REPO/drivers/hdf_core/interfaces/inner_api/utils"

echo "[cc] key_demo.c"
"$CLANG" $FLAGS $SYS $HDF_I -c key_demo.c -o key_demo.o

# 注意:显式给 .a 路径,避免 -lkeydriver 同目录存在 .so 时被优先动态链接
echo "[ld] key_demo (静态链 libkeydriver.a + 动态链 libhdf_platform.so)"
"$CLANG" $FLAGS $SYS -o key_demo key_demo.o ./libkeydriver.a -L"$OUT/usr/lib" -lhdf_platform

echo "done: $(ls -l key_demo key_demo.o | awk '{print $NF, $5}')"
