#!/bin/bash
# build_ui_display.sh - 独立交叉编译配套屏 UI 程序 ui_display(不依赖源码树 GN)
# 用法: bash build_ui_display.sh [OH源码根目录]   (默认 /code/3516)
# 前置: 框架共享库在 $OUT/usr/lib(libui/libwms_client/libfreetype/... 镜像自带)
# 产物: ui_display(板端程序, push /bin 后 exec 调用) + 4 个中间 .o
set -e
cd "$(dirname "$0")"

REPO="${1:-/code/3516}"
OUT="$REPO/out/hispark_taurus/ipcamera_hispark_taurus"
CLANG="$REPO/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang"

FLAGS="--target=arm-liteos-eabi -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 -O2 -Wall"
SYS="--sysroot=$OUT/sysroot"
# ui_lite / graphic_utils_lite / freetype 头(与树内 BUILD.gn include_dirs 等价的全集)
UI_I="-I$REPO/foundation/arkui/ui_lite/frameworks \
      -I$REPO/foundation/arkui/ui_lite/interfaces/kits \
      -I$REPO/foundation/arkui/ui_lite/interfaces/innerkits \
      -I$REPO/foundation/graphic/graphic_utils_lite/interfaces/kits \
      -I$REPO/foundation/graphic/graphic_utils_lite/interfaces/innerkits \
      -I$REPO/foundation/graphic/graphic_utils_lite/frameworks \
      -I$REPO/third_party/freetype/include \
      -I$REPO/third_party/bounds_checking_function/include"

for s in ui_display.cpp ui_design.cpp emoji_display.cpp gif_decoder.c; do
    echo "[cc] $s"
    "$CLANG" $FLAGS $SYS $UI_I -c "$s" -o "${s%.*}.o"
done

echo "[ld] ui_display (动态链 UI 框架 so, 板上 /usr/lib 自带)"
"$CLANG" $FLAGS $SYS -o ui_display ui_display.o ui_design.o emoji_display.o gif_decoder.o \
    -L"$OUT/usr/lib" \
    -Wl,-rpath-link="$OUT/usr/lib" \
    -lui -lwms_client -lfreetype -lgraphic_utils_lite -lgraphic_hals -lsurface \
    -lhilog_shared -lsec_shared -lc++ -lpthread

echo "done: $(ls -l ui_display *.o | awk '{print $NF, $5}')"
