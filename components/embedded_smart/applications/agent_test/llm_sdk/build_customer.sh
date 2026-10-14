#!/bin/bash
# 客户验收用例编译 — 完全按客户接入方式: 流式库(../llm_stream) + libwificlient.a 静态链
# 头文件搜索顺序: -I../llm_stream 在前(llm_client.h 取 v1.3, 含 llm_chat_stream)
set -e
R=/code/3516
OUT=$R/out/hispark_taurus/ipcamera_hispark_taurus
D=$(cd "$(dirname "$0")" && pwd)
S=$D/../llm_stream
CLANG=$R/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang
FLAGS="--target=arm-liteos-eabi -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 -O2 -Wall"
SYSROOT=$OUT/sysroot
$CLANG $FLAGS --sysroot=$SYSROOT -I"$S" -I"$D" "$D/customer_test.c" "$S/libllmclient.a" "$D/libwificlient.a" -L$OUT/usr/lib -lmbedtls -o "$D/customer_test"
ls -la "$D/customer_test"
echo "BUILD OK"
