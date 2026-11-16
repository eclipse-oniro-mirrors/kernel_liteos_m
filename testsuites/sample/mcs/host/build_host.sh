#!/usr/bin/env bash
# build_host.sh — mcs host 工具编译（2026-11-09 新架构）
# 产物：host_server + mcs_cmd（板 /data）
# 工具链：OH clang + musl sysroot（MUSL 环境变量可覆盖）
# libmica：master-code staging（RK3568_MASTER_ROOT 可覆盖）
set -eu
HOST_SRC=$(cd "$(dirname "$0")" && pwd)
MASTER=${RK3568_MASTER_ROOT:-/home/t30028416/test/MAINTEST/master-code}
CODE=$MASTER
MUSL=${MUSL:-/home/t30028416/test_tools/rk3568_host_deps/musl_sysroot}
OUT=${OUT:-$HOST_SRC/out}
SOC=$CODE/device/soc/rockchip/rk3568/mcs
LLVMBIN=$CODE/prebuilts/clang/ohos/linux-x86_64/llvm/bin

INC="-I$SOC/libmica/include -I$SOC/prebuilt/include -I$HOST_SRC/.."
LIB="-L$SOC/out/staging/lib -L$SOC/prebuilt/lib"
TFLAGS="--target=aarch64-linux-ohos --sysroot=$MUSL -I$MUSL/include/aarch64-linux-ohos -B$MUSL/lib/aarch64-linux-ohos -L$MUSL/lib/aarch64-linux-ohos"
LDFLAGS="-Wl,--dynamic-linker=/data/lib/ld-musl-aarch64.so.1"

mkdir -p $OUT
echo "[build_host] host_server ..."
$LLVMBIN/clang $TFLAGS $INC -O2 -Wall -Wno-unused-parameter \
    $LDFLAGS -o $OUT/host_server \
    $HOST_SRC/host_server.c $HOST_SRC/host_slave.c $HOST_SRC/host_passive.c \
    $LIB -lmica -l:libmetal.so.1 -l:libopen_amp.so.1 -lmica_compat -lpthread

echo "[build_host] mcs_cmd ..."
$LLVMBIN/clang $TFLAGS -O2 -Wall -static -o $OUT/mcs_cmd $HOST_SRC/mcs_cmd.c

echo BUILD_OK
$LLVMBIN/llvm-readelf -h $OUT/host_server | grep -E "Class:|Machine:"
