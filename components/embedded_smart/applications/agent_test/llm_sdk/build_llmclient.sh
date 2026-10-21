#!/bin/bash
# llm_client 流式库(v1.3) + 双示例 编译 — liteos_a (musl + mbedTLS)
# 库源在同级 ../llm_stream(llm_client.c + llm_stream.c, 含 llm_chat_stream),
# 库产物 .o/.so/.a 输出到 ../llm_stream; 本目录原 libllmclient.a/.so(v1.1
# 非流式)保留不动, 不再由本脚本产出。llm_stream 目录须与本目录同级放置。
set -e
R=/code/3516
OUT=$R/out/hispark_taurus/ipcamera_hispark_taurus
D=$(cd "$(dirname "$0")" && pwd)          # 本脚本所在目录(llm_sdk)
S=$D/../llm_stream                        # 流式库源/产物目录
CLANG=$R/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang
AR=$R/prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-ar
FLAGS="--target=arm-liteos-eabi -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 -O2 -Wall"
SYSROOT=$OUT/sysroot
MBEDINC=$R/third_party/mbedtls/include
cd "$S"
# 1. 对象
$CLANG $FLAGS --sysroot=$SYSROOT -I. -I$MBEDINC -fPIC -c llm_client.c -o llm_client.o
$CLANG $FLAGS --sysroot=$SYSROOT -I. -I$MBEDINC -fPIC -c llm_stream.c -o llm_stream.o
# 2. 动态库(mbedtls 动态链, 板上 rootfs 自带 libmbedtls.so)
$CLANG $FLAGS --sysroot=$SYSROOT -shared llm_client.o llm_stream.o -o libllmclient.so -L$OUT/usr/lib -lmbedtls
# 3. 静态库(客户静态链时需自行追加 -lmbedtls)
$AR rcs libllmclient.a llm_client.o llm_stream.o
# 4. 示例1: chat(网关直连, llm_chat_stream 流式演示) — 静态链库, 免运行时查找
$CLANG $FLAGS --sysroot=$SYSROOT -I"$S" -I$MBEDINC "$D/chat.c" "$S/libllmclient.a" -L$OUT/usr/lib -lmbedtls -o "$D/chat"
# 5. 示例2: chat_direct(客户网关直连, llm_chat_stream 流式演示) — 静态链库
$CLANG $FLAGS --sysroot=$SYSROOT -I"$S" -I$MBEDINC "$D/chat_direct.c" "$S/libllmclient.a" -L$OUT/usr/lib -lmbedtls -o "$D/chat_direct"
ls -la "$S/llm_client.o" "$S/llm_stream.o" "$S/libllmclient.so" "$S/libllmclient.a" "$D/chat" "$D/chat_direct"
echo "BUILD OK"
