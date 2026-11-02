#!/usr/bin/env bash
# Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of
# conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list of
# conditions and the following disclaimer in the documentation and/or other materials
# provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may be used
# to endorse or promote products derived from this software without specific prior written
# permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# ============================================================================
# analyze_size.sh - 镜像体积分析方法（板无关，纯主机侧，只读）
#
# 用法:
#   analyze_size.sh <ELF>                单镜像: 段汇总 + 目录归属 + TOP 符号
#   analyze_size.sh <旧ELF> <新ELF>      对比模式: 段/目录/文件大小 delta
#
# 输出:
#   [1] 段汇总(size -A 全量, 标注 SRAM 段/调试段; 板自定义段名自动适配)
#   [2] 目录归属: 遍历 ELF 同级 out 目录的 obj/**/*.o, 按源目录聚合体积
#       —— 链接器无关(GNU ld / lld / LTO 前均可), 反映"源码体积分布"
#   [3] TOP 符号(nm 按大小排序): 定位体积大头(大缓冲/大表)
#   [4] 对比模式: 两镜像段级 delta + 目录归属 delta + 文件大小 delta
#
# 工具链: 主机 nm/size(多目标 binutils); 不依赖板侧工具链
# ============================================================================
set -uo pipefail

NM="$(command -v nm)"
SIZE="$(command -v size)"
if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "用法: $0 <ELF> [对比ELF]" >&2
    exit 1
fi
ELF1="$1"
ELF2="${2:-}"
for f in "$ELF1" ${ELF2:+"$ELF2"}; do
    [ -f "$f" ] || { echo "错误: ELF 不存在: $f" >&2; exit 1; }
done

# ---- 目录归属: obj 相对路径 → 顶级桶 ----
bucket_of() {
    case "$1" in
        *kernel/liteos_m/testsuites*) echo "testsuites" ;;
        *kernel/liteos_m/kernel*)     echo "kernel" ;;
        *kernel/liteos_m/arch*)       echo "arch" ;;
        *kernel/liteos_m/lib*)        echo "lib(posix/cmsis)" ;;
        *kernel/liteos_m/compat*)     echo "compat" ;;
        *kernel/liteos_m/targets*)    echo "targets" ;;
        *kernel/liteos_m*)            echo "kernel其他" ;;
        *device/*)                    echo "device" ;;
        *musl*)                       echo "musl" ;;
        *third_party*)                echo "third_party其他" ;;
        *)                            echo "其他" ;;
    esac
}

# ---- [1] 段汇总 ----
sec_summary() {
    local elf="$1" tag="$2"
    printf "== [%s] %s (文件 %s 字节 = %.2f MB)\n" "$tag" "$(basename "$elf")" \
        "$(stat -c%s "$elf")" "$(echo "scale=2; $(stat -c%s "$elf")/1048576" | bc)"
    "$SIZE" -A "$elf" 2>/dev/null | awk '
        NR > 2 && NF >= 3 && $2 ~ /^[0-9]+$/ && $2 > 0 {
            name = $1; sz = $2
            if (name ~ /^\.debug|^\.comment|^\.riscv|^\.ARM\.attributes|^\.pdr/) cls = "调试/属性段(不占RAM)"
            else if (name ~ /\.bss$|^\.stack|\.heap$|\.data$|_bss|_data/)     cls = "SRAM段"
            else                                                               cls = "加载段(代码/常量)"
            s[cls] += sz; lines[name] = sz
        }
        END {
            for (k in s) printf "   %-24s %10d\n", k, s[k]
            printf "   -- 明细(前12大段):\n"
            for (n in lines) print lines[n], n
        }' | sort --reverse --numeric-sort | awk '
        !/^ / { printf "%s\n", $0; next }
        { printf "%s\n", $0 }' | head -3
    "$SIZE" -A "$elf" 2>/dev/null | awk 'NR > 2 && NF >= 3 && $2 ~ /^[0-9]+$/ && $2 > 0 {
            print $2, $1 }' | sort --reverse --numeric-sort | head -12 | awk '{ printf "   %10d  %s\n", $1, $2 }'
}

# ---- [2] 目录归属: 遍历 obj 树 ----
find_objdir() {
    local d
    d="$(dirname "$1")"
    while [ "$d" != "/" ]; do
        [ -d "$d/obj" ] && { echo "$d/obj"; return; }
        d="$(dirname "$d")"
    done
    return 1
}

attr_summary() {
    local elf="$1" tag="$2" outfile="$3"
    local objdir
    objdir="$(find_objdir "$elf")" || { printf "== [%s] 目录归属: 未找到 obj 目录, 跳过\n" "$tag"; return; }
    printf "== [%s] 目录归属(源码级, 遍历 %s):\n" "$tag" "$objdir"
    find "$objdir" -name '*.o' ! -name '*thincache*' 2>/dev/null | while IFS= read -r o; do
        echo "$o"
    done | xargs -r -n 64 "$SIZE" -A 2>/dev/null | awk -v objdir="$objdir" '
        /^[^ ]/ && / :$/ { cur = $1; next }
        NF >= 3 && $2 ~ /^[0-9]+$/ && $1 !~ /^\.debug|^\.comment|^\.riscv|^\.ARM/ {
            sz[cur] += $2 + 0
        }
        END {
            for (f in sz) {
                rel = f
                sub(objdir "/", "", rel)
                n = split(rel, parts, "/")
                path = ""
                for (i = 1; i < n; i++) path = path (i > 1 ? "/" : "") parts[i]
                print sz[f] "\t" path
            }
        }' | while IFS=$'\t' read -r sz path; do
            b=$(bucket_of "$path")
            echo "$b $sz"
        done | awk -v out="$outfile" '{ s[$1] += $2; t += $2 }
        END {
            for (k in s) printf "%12d  %6.2f%%  %s\n", s[k], (t ? s[k]*100/t : 0), k | "sort --reverse --numeric-sort"
            printf "%12d  %6.2f%%  TOTAL\n", t, 100
            for (k in s) print k "\t" s[k] > out
        }'
}

# ---- [3] TOP 符号 ----
top_symbols() {
    local elf="$1" tag="$2" n="${3:-30}"
    printf "== [%s] TOP%d 大符号:\n" "$tag" "$n"
    "$NM" -S --size-sort --radix=d "$elf" 2>/dev/null | awk '{ sz = $2 + 0; if (sz > 0) print sz "\t" $NF }' \
        | sort --reverse --numeric-sort | head -"$n" | awk '{ printf "   %10d  %s\n", $1, $2 }'
}

# ---- [4] 对比 ----
compare() {
    local a="$1" b="$2"
    printf "== [对比] 段级 delta:\n"
    "$SIZE" -A "$a" | awk 'NR>2 && NF>=3 {v[$1]=$2} END{for(k in v) print k, v[k]}' | sort > /tmp/.as_a
    "$SIZE" -A "$b" | awk 'NR>2 && NF>=3 {v[$1]=$2} END{for(k in v) print k, v[k]}' | sort > /tmp/.as_b
    join /tmp/.as_a /tmp/.as_b | awk '{ d = $3 - $2; if (d != 0) printf "   %-24s %10d → %-10d (%+d)\n", $1, $2, $3, d }' | sort -t'(' -k2 --reverse --numeric-sort | head -15
    awk 'NR==FNR{v[$1]=$2;next} !(($1) in v) {print $1, 0, $2}' /tmp/.as_a /tmp/.as_b | \
        awk '$3 > 0 { printf "   %-24s %10d → %-10d (%+d) [新增段]\n", $1, $2, $3, $3 }' | head -5
    rm -f /tmp/.as_a /tmp/.as_b
    printf "== [对比] 文件大小: %d → %d (%+d 字节)\n" \
        "$(stat -c%s "$a")" "$(stat -c%s "$b")" "$(( $(stat -c%s "$b") - $(stat -c%s "$a") ))"
    printf "== [对比] 目录归属 delta(源码级):\n"
    attr_delta /tmp/.as_attr_a /tmp/.as_attr_b
}

attr_delta() {
    awk -F'\t' 'NR==FNR{v[$1]=$2;next} { d=$2-(($1) in v ? v[$1] : 0); if(d!=0) printf "%12d  %s\n", d, $1 }' "$1" "$2" | sort --reverse --numeric-sort
}

# ---- 主流程 ----
[ -x "$NM" ] && [ -x "$SIZE" ] || { echo "错误: 需要 nm/size" >&2; exit 1; }
sec_summary "$ELF1" "基准"
attr_summary "$ELF1" "基准" /tmp/.as_attr_a
top_symbols "$ELF1" "基准" 30
if [ -n "$ELF2" ]; then
    echo
    sec_summary "$ELF2" "对比"
    attr_summary "$ELF2" "对比" /tmp/.as_attr_b
    echo
    compare "$ELF1" "$ELF2"
    top_symbols "$ELF2" "对比" 15
fi
