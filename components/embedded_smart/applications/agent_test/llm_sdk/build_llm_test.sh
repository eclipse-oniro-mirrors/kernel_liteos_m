#!/bin/bash
# llm_test 编译 — liteos_a (musl + mbedTLS), 静态链流式库(../llm_stream) + libwificlient.a
# 头文件搜索顺序: -I../llm_stream 在前(llm_client.h 取 v1.3, 含 llm_chat_stream)
# 用法: bash build_llm_test.sh   (产物 llm_test 在本目录, 可直接放板上 /userdata 或重打包进 rootfs)
set -e
R=/code/3516
OUT=$R/out/hispark_taurus/ipcamera_hispark_taurus
D=$(cd "$(dirname "$0")" && pwd)
S=$D/../llm_stream
CLANG=$R/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang
FLAGS="--target=arm-liteos-eabi -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 -O2 -Wall"
SYSROOT=$OUT/sysroot
$CLANG $FLAGS --sysroot=$SYSROOT -I"$S" -I"$D" "$D/llm_test.c" "$S/libllmclient.a" "$D/libwificlient.a" \
    -L$OUT/usr/lib -lmbedtls -o "$D/llm_test"
ls -la "$D/llm_test"
echo "BUILD OK"
