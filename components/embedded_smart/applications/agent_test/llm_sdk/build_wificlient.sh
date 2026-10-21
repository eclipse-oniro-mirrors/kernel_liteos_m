#!/bin/bash
# wifi_client 库 + 示例 编译 — liteos_a (musl only)
set -e
R=/code/3516
OUT=$R/out/hispark_taurus/ipcamera_hispark_taurus
W=$R/cap_test
CLANG=$R/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang
AR=$R/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-ar
FLAGS="--target=arm-liteos-eabi -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 -O2 -Wall"
SYSROOT=$OUT/sysroot
cd $W
$CLANG $FLAGS --sysroot=$SYSROOT -I. -fPIC -c wifi_client.c -o wifi_client.o
$CLANG $FLAGS --sysroot=$SYSROOT -shared wifi_client.o -o libwificlient.so
$AR rcs libwificlient.a wifi_client.o
$CLANG $FLAGS --sysroot=$SYSROOT -I. wifi_test.c libwificlient.a -o wifi_test
ls -la wifi_client.o libwificlient.so libwificlient.a wifi_test
echo "BUILD OK"
