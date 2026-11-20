#!/bin/bash
# ws63_tier_compare.sh - ws63 三挡位(TINY/SMALL/STANDARD) RAM/ROM 预期 vs 实测对比 → Excel
#
# 预期值: kernel/liteos_m/kernel/Kconfig 挡位 help 标称(TINY ~2KB / SMALL ~5KB / STANDARD ~10KB RAM,
#         脚本动态解析 help 原文, 板无关); ROM 实现与说明书均未定义标称 → N/A 不作符合性判定
# 实际值: RAM = kernel/liteos_m/tools/footprint/footprint.py 内核域口径
#               (静态符号 + 栈 + 动态申请 + 节点开销, 堆池不计入)
#         ROM = 本目录 gen_flash_size.py nm 符号级 text+rodata
#               (活产物 obj/ 逐 .o 扫描 + map 汇编兜底, ws63 flash 512KB, 不含 .data)
# 数据源: tier_meas/ws63_{tiny,small,standard}/ 归档齐备 → 自动复用(footprint 现场重测, ROM 取构建期产物);
#         缺失/--fresh → 现场构建(仅测量守卫 → 挡位纯净 config → 物理清理全量构建 → gen_flash_size → 还原)
# 规避可见: 守卫2生效(缺陷在位)时写 tier_meas/workaround.marker → Excel 测量说明/夜间摘要/WeLink 推送
#           均自动带"规避备注"(原因/规避动作/待修复自动撤销); 上游修复后锚点 miss → 标记清除, 备注消失
# 互斥:   与 nightly_case_replay.sh 共用 /tmp/nightly_case_replay.lock(flock -w 7200 自动排队不互踩);
#         被 nightly 直接调用时以 TIER_CMP_NO_LOCK=1 跳过(锁由父进程持有, 避免同锁自阻塞)
# 定位:   ROOT 按脚本自身位置自定位(本文件所在 preset 目录上溯 6 级 = 工程根, 同 os_test.py 口径),
#         全部执行目录均为 ROOT 相对路径, 不绑定机器绝对路径
# 输出:   当前目录/ws63挡位预期对比.xlsx(默认落在脚本执行时的 $PWD, --out 可覆盖; 超出标称 → ⚠ warning 红底标识)
# 用法:   bash ws63_tier_compare.sh [--out 路径.xlsx] [--fresh]    # --fresh 强制现场构建
#         SKIP_REBUILD=1 bash ws63_tier_compare.sh                 # 构建后不重建 ws63 基线镜像
# 前置:   backup/ws63_debug.config.baseline(骨架回退源, nightly 阶段2 刷新);
#         itp002 挡位 config 可选(ItPreset002 产物, 阶段3 未跑时自动回退基线骨架白名单自生成);
#         ws63 riscv SDK 工具链; backup/ws63_debug.config.baseline; openpyxl

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # .../kernel/liteos_m/testsuites/sample/tools/preset
ROOT="$(cd "$HERE/../../../../../.." && pwd)"          # preset 上溯 6 级 = 工程根(自定位, 不绑定绝对路径)
export ROOT                                             # 供内嵌 python heredoc 解析同一 ROOT
RUN=$ROOT/testresult
TM=$RUN/tier_meas
GUARD_BAK=$RUN/guard_backup
ITP=$RUN/preset/itp002
RV_BIN=$ROOT/device/soc/hisilicon/ws63v100/sdk/tools/bin/compiler/riscv/cc_riscv32_musl_100/cc_riscv32_musl_fp/bin
FP=$ROOT/kernel/liteos_m/tools/footprint/footprint.py
GFS=$HERE/gen_flash_size.py
KLM=$ROOT/kernel/liteos_m
WV=$ROOT/vendor/hihope/ws63_m_testsuits/kernel_configs/debug.config
WBL=$RUN/backup/ws63_debug.config.baseline
WOUT=$ROOT/out/ws63_m/ws63_m_testsuits
WPRE=$ROOT/out/preloader/ws63_m_testsuits
LOG=$RUN/ws63_tier_compare.log
XLSX=$PWD/ws63挡位预期对比.xlsx   # 默认输出到执行时所在目录(当前目录), --out 可覆盖
FRESH=0

export PATH=$RV_BIN:/usr/local/bin:/usr/bin:/bin:$PATH

while [ $# -gt 0 ]; do
  case "$1" in
    --out)   XLSX=$2; shift 2 ;;
    --fresh) FRESH=1; shift ;;
    *)       echo "未知参数 $1 (可用: --out 路径.xlsx / --fresh)"; exit 1 ;;
  esac
done

: > "$LOG"
log() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOG"; }
die() { log "FATAL: $*"; exit 1; }

if [ "${TIER_CMP_NO_LOCK:-0}" = 1 ]; then
  log "由父流水线(nightly)调用, 跳过任务锁(父进程已持锁)"
else
  exec 9>/tmp/nightly_case_replay.lock
  log "等待任务锁(与 nightly 排队, 最长 2h)..."
  flock -w 7200 9 || die "任务锁等待超时(nightly 仍在运行?)"
fi

for f in "$FP" "$GFS" "$WV" "$WBL"; do
  [ -f "$f" ] || die "缺少前置文件 $f"
done
# itp002 挡位 config 非硬性前置: 缺失(阶段3 未跑)时自动回退 vendor 基线骨架白名单自生成
[ -d "$RV_BIN" ] || die "缺少 ws63 riscv SDK 工具链 $RV_BIN"
python3 -c "import openpyxl" 2>/dev/null || die "缺少 openpyxl"

artifacts_complete() {
  [ "$FRESH" = 1 ] && return 1
  local d t
  for t in tiny small standard; do
    d=$TM/ws63_$t
    [ -f "$d/OHOS_Image" ] && [ -f "$d/config.h" ] || return 1
    [ -s "$d/flash_total.txt" ] && [ "$(cat "$d/flash_total.txt" 2>/dev/null)" -gt 0 ] 2>/dev/null || return 1
  done
  return 0
}

BUILD_MODE=0
TCFG=$KLM/targets/ws63_m/liteos_m/board/target_config.h
restore_all() {
  if [ "$BUILD_MODE" = 1 ]; then
    log "== 还原仅测量守卫与 vendor 基线 =="
    cp -f "$GUARD_BAK/los_init.c.orig" "$KLM/kernel/base/los_init.c" 2>/dev/null
    [ -f "$GUARD_BAK/ws63_targets_target_config.h.orig" ] && \
      cp -f "$GUARD_BAK/ws63_targets_target_config.h.orig" "$TCFG" 2>/dev/null
    cp -f "$WBL" "$WV"
    if diff -q "$GUARD_BAK/los_init.c.orig" "$KLM/kernel/base/los_init.c" >/dev/null 2>&1 && \
       { [ ! -f "$GUARD_BAK/ws63_targets_target_config.h.orig" ] || \
         diff -q "$GUARD_BAK/ws63_targets_target_config.h.orig" "$TCFG" >/dev/null 2>&1; } && \
       diff -q "$WBL" "$WV" >/dev/null 2>&1; then
      log "守卫还原核验一致"
    else
      log "WARNING: 还原后仍有差异, 请人工核对 guard_backup/"
    fi
    if [ "${SKIP_REBUILD:-0}" != 1 ]; then
      log "== 终态 ws63 基线重建 =="
      if ( cd "$ROOT" && hb build -p ws63_m_testsuits -f >>"$LOG" 2>&1 ); then
        log "ws63 基线重建完成"
      else
        log "WARNING: 基线重建失败(详见 $LOG)"
      fi
    fi
  fi
}
trap restore_all EXIT

MODE=reuse
if artifacts_complete; then
  log "数据源: 复用 $TM/ws63_{tiny,small,standard} 归档(footprint 现场重测, ROM 取构建期 gen_flash_size 产物)"
else
  MODE=build
  BUILD_MODE=1
  log "数据源: 现场构建挡位纯净测量镜像 × 3"
  [ -f "$GUARD_BAK/los_init.c.orig" ] || { mkdir -p "$GUARD_BAK"; \
    cp "$KLM/kernel/base/los_init.c" "$GUARD_BAK/los_init.c.orig"; }
  log "== 施加仅测量守卫(los_init.c g_swtmrExpiryBuf + 板级 SWTMR 硬编码接管) =="
  python3 - <<'PYEOF' || die "守卫施加失败"
import os, shutil

ROOT = os.environ["ROOT"]
KLM = f"{ROOT}/kernel/liteos_m"
GB = f"{ROOT}/testresult/guard_backup"
os.makedirs(GB, exist_ok=True)

def backup(src, name):
    if not os.path.exists(f"{GB}/{name}"):
        shutil.copy(src, f"{GB}/{name}")

# 守卫1: los_init.c g_swtmrExpiryBuf 条件接管(INITCALL 期间不分配)
p = f"{KLM}/kernel/base/los_init.c"
backup(p, "los_init.c.orig")
t = open(p).read()
old = '#if (LOSCFG_BASE_CORE_SWTMR == 1) && !defined(LOSCFG_BASE_CORE_SWTMR_IN_ISR)\nLITE_OS_SEC_BSS STATIC UINTPTR g_swtmrExpiryBuf'
new = '#if (LOSCFG_BASE_CORE_SWTMR == 1) && !defined(LOSCFG_BASE_CORE_SWTMR_IN_ISR) && (LOSCFG_KERNEL_INITCALL != 1)\nLITE_OS_SEC_BSS STATIC UINTPTR g_swtmrExpiryBuf'
if new in t:
    print("los_init guard already applied")
elif old in t:
    open(p, "w").write(t.replace(old, new, 1))
    print("los_init guard applied")
else:
    raise SystemExit("los_init.c anchor not found")

# 守卫2: 板级 target_config.h SWTMR 硬编码中和(87a563bd 经 !1242 复活缺陷④⑤;
#   C 宏=1 而 Kconfig/.config=n → TINY 断链; 中和后 SWTMR 交回 Kconfig 全权接管)
p = f"{KLM}/targets/ws63_m/liteos_m/board/target_config.h"
backup(p, "ws63_targets_target_config.h.orig")
t = open(p).read()
pairs = [
    ("#define LOSCFG_BASE_CORE_SWTMR                              1",
     "/* measurement-only: SWTMR 由 Kconfig 全权接管 */"),
    ("#define LOSCFG_BASE_CORE_SWTMR_ALIGN                        1",
     "/* measurement-only: SWTMR_ALIGN 由 Kconfig 全权接管 */"),
]
hit = 0
for old, new in pairs:
    if old in t:
        t = t.replace(old, new, 1)
        hit += 1
    elif new in t:
        hit += 1
if hit == 2:
    open(p, "w").write(t)
    print("board swtmr hardcode neutralized (%d/2)" % hit)
elif hit == 0:
    print("board swtmr hardcode absent (上游已修, 无需守卫)")
else:
    raise SystemExit("target_config.h 硬编码锚点部分命中(%d/2), 请人工核对" % hit)

# 规避标记: 守卫2生效(缺陷在位) → 写 marker, 供数据生成/Excel/夜间摘要/WeLink 推送备注;
#           上游已修(锚点 miss) → 清除 marker, 备注随之消失(规避自动撤销, 无需人工回改)
MARK = f"{ROOT}/testresult/tier_meas/workaround.marker"
if hit == 2:
    os.makedirs(os.path.dirname(MARK), exist_ok=True)
    open(MARK, "w").write(
        "TINY 构建断链缺陷在位(问题记录 20261118_091747: 上游 87a563bd 经 !1242 复活 ws63 板级"
        " SWTMR 硬编码, C 宏=1 而 Kconfig=n → TINY 链接失败无数据); 本轮已用仅测量守卫规避出数:"
        " 构建期中和 target_config.h 两行硬编码(SWTMR 交回 Kconfig 接管), 测后还原并 diff 核验;"
        " 待问题修复后守卫锚点自动失效跳过, 规避自动撤销, 本备注随之消失")
    print("workaround marker written (守卫生效, 报告将带规避备注)")
else:
    if os.path.exists(MARK):
        os.remove(MARK)
    print("workaround marker cleared (上游已修, 无规避备注)")
PYEOF

  if [ -f "$ITP/ws63_tiny.config" ] && [ -f "$ITP/ws63_small.config" ] && [ -f "$ITP/ws63_standard.config" ]; then
    log "挡位 config 源: itp002(savemenuconfig 产物)"
  else
    log "itp002 挡位 config 缺失(阶段3 未跑), 回退 vendor 基线骨架白名单自生成"
  fi
  python3 - <<'PYEOF' || die "测量 config 生成失败"
import os, re
ROOT = os.environ["ROOT"]
SRC = f"{ROOT}/testresult/preset/itp002"
BASE = f"{ROOT}/testresult/backup/ws63_debug.config.baseline"
OUT = f"{ROOT}/testresult/tier_meas"
os.makedirs(OUT, exist_ok=True)
KEEP = re.compile(r"^LOSCFG_(PLATFORM_|ARCH_|CORTEX_M_|DW_APB_TIMER|KERNEL_RAM_PRESET|KERNEL_TICK|BASE_CORE_TSK_DEFAULT_STACK_SIZE)")
# 回退骨架白名单: 仅板/架构/时基骨架行; 显式排除 PLATFORM_EXC / PLATFORM_HWI_LIMIT 等
# vendor 调优显式行(否则中断表 96 条/异常钩子显式开, 测量口径改变)
SKELETON_KEYS = ("LOSCFG_PLATFORM_WS63_M", "LOSCFG_ARCH_CACHE", "LOSCFG_ARCH_INTERRUPT_PREEMPTION",
                 "LOSCFG_KERNEL_TICK_PERIODIC", "LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE",
                 "LOSCFG_DW_APB_TIMER")
itp_ok = all(os.path.exists(f"{SRC}/ws63_{t}.config") for t in ("tiny", "small", "standard"))
for tier in ("tiny", "small", "standard"):
    if itp_ok:
        lines = [l.rstrip("\n") for l in open(f"{SRC}/ws63_{tier}.config")
                 if not l.startswith("LOSCFG_") or KEEP.match(l)]
    else:
        lines = [l.rstrip("\n") for l in open(BASE) if l.startswith(SKELETON_KEYS)]
        lines += ["LOSCFG_KERNEL_RAM_PRESET=y", f"LOSCFG_KERNEL_RAM_PRESET_{tier.upper()}=y"]
    lines += ["# LOSCFG_KERNEL_TEST is not set", "# LOSCFG_NET_LWIP is not set"]
    # TINY 不补 IPC_MUX: ws63 板 printf 走内核 base.los_printf.o + UartVprintf, 无 musl stdio/
    # pthread 符号入镜像(实测 0 引用), Kconfig 挡位默认 MUX=n; 强开 144B 动态+12B 静态纯死重
    open(f"{OUT}/ws63_{tier}_meas.config", "w").write("\n".join(lines) + "\n")
print("meas configs written (source: %s)" % ("itp002" if itp_ok else "baseline-skeleton"))
PYEOF

  FAILS=0
  for t in tiny small standard; do
    log "== 构建 ws63 $t(物理清理全量) =="
    cp "$TM/ws63_${t}_meas.config" "$WV"
    rm -rf "$WOUT" "$WPRE"
    if ( cd "$ROOT" && hb build -p ws63_m_testsuits -f ) >>"$LOG" 2>&1; then
      d=$TM/ws63_$t; rm -rf "$d"; mkdir -p "$d"
      cp "$WOUT/OHOS_Image" "$WOUT/OHOS_Image.map" "$WOUT/config.h" "$WOUT/toolchain.ninja" "$d/"
      if python3 "$GFS" "$WOUT" --nm "$RV_BIN/riscv32-linux-musl-nm" --flash-size 524288 \
           > "$d/flash_size.txt" 2>"$d/flash_size.err" && \
         grep -oE "Flash 总占用 \(text\+rodata\): [0-9]+" "$d/flash_size.txt" | grep -oE "[0-9]+$" > "$d/flash_total.txt" && \
         [ -s "$d/flash_total.txt" ]; then
        log "BUILD OK: ws63 $t (ROM[nm text+rodata]=$(cat "$d/flash_total.txt")B)"
      else
        log "BUILD OK: ws63 $t (WARNING: gen_flash_size 未产出, Excel 将回退 size 口径)"
      fi
    else
      log "BUILD FAIL: ws63 $t (见 $LOG)"
      FAILS=$((FAILS+1))
    fi
  done
  [ $FAILS -eq 0 ] || die "有 $FAILS 个挡位构建失败"
fi

log "== RAM 实测(footprint) + 预期解析(Kconfig) + Excel 生成 =="
python3 - "$XLSX" "$MODE" <<'PYEOF' 2>&1 | tee -a "$LOG" || die "对比/Excel 生成失败"
import datetime, json, os, re, subprocess, sys

XLSX, MODE = sys.argv[1], sys.argv[2]
ROOT = os.environ["ROOT"]
TM = f"{ROOT}/testresult/tier_meas"
KLM = f"{ROOT}/kernel/liteos_m"
FP = f"{KLM}/tools/footprint/footprint.py"
RV = f"{ROOT}/device/soc/hisilicon/ws63v100/sdk/tools/bin/compiler/riscv/cc_riscv32_musl_100/cc_riscv32_musl_fp/bin/riscv32-linux-musl-size"
FALLBACK = {"tiny": 2048, "small": 5120, "standard": 10240}
LAND = {"tiny": (1, 128), "small": (6, 512), "standard": (16, 512)}
TIERS = ("tiny", "small", "standard")
TIER_LABEL = {"tiny": "极小MCU-TINY", "small": "小型设备-SMALL", "standard": "标准嵌入式-STANDARD"}

kc = open(f"{KLM}/kernel/Kconfig", errors="replace").read()
nominal, helps = {}, {}
for t in TIERS:
    m = re.search(rf"config KERNEL_RAM_PRESET_{t.upper()}\b(.*?)(?=\nconfig |\nif |\Z)", kc, re.S)
    h = ""
    if m:
        hm = re.search(r"help\n(.*?)(?=\nconfig |\nif |\Z)", m.group(1), re.S)
        h = " ".join(l.strip() for l in (hm.group(1) if hm else "").splitlines() if l.strip())
    helps[t] = h
    km = re.search(r"about (\d+(?:\.\d+)?)\s*KB of RAM", h)
    nominal[t] = int(float(km.group(1)) * 1024) if km else FALLBACK[t]
    if not km:
        print(f"[WARN] Kconfig help 未解析到标称, {t} 回退 {FALLBACK[t]}B", file=sys.stderr)

rows = []
for t in TIERS:
    d = f"{TM}/ws63_{t}"
    r = subprocess.run(["python3", FP, "--out", d, "--output", f"{d}/fp"],
                       capture_output=True, text=True)
    txt = r.stdout + r.stderr
    m = re.search(r"RAM 合计 \(不含节点开销\): (\d+)", txt)
    ram_total = int(m.group(1)) if m else None
    comp = {}
    sj = f"{d}/fp/summary.json"
    if os.path.exists(sj):
        tot = json.load(open(sj)).get("totals", {})
        comp = {k: tot.get(k) for k in ("ram_static", "stack", "dyn", "overhead")}
    else:
        m = re.search(r"RAM 构成: 静态符号 (\d+) \+ 栈 (\d+) \+ 动态申请 (\d+) \(\+ 开销 (\d+)\)", txt)
        if m:
            comp = dict(zip(("ram_static", "stack", "dyn", "overhead"), map(int, m.groups())))
    rom, rom_src = None, "nm text+rodata"
    ft = f"{d}/flash_total.txt"
    if os.path.exists(ft) and open(ft).read().strip().isdigit() and int(open(ft).read().strip()) > 0:
        rom = int(open(ft).read().strip())
    sz = subprocess.run([RV, f"{d}/OHOS_Image"], capture_output=True, text=True).stdout
    last = sz.strip().splitlines()[-1].split()
    text_, data_, bss_ = (int(last[i]) for i in (0, 1, 2))
    if rom is None:
        rom, rom_src = text_ + data_, "size text+data(回退)"
    ch = open(f"{d}/config.h", errors="replace").read()
    tsk = re.search(r"#define LOSCFG_BASE_CORE_TSK_LIMIT (\d+)", ch)
    idle = re.search(r"#define LOSCFG_BASE_CORE_TSK_IDLE_STACK_SIZE (\d+)", ch)
    rows.append({
        "tier": t, "ram_total": ram_total, "comp": comp, "rom": rom, "rom_src": rom_src,
        "text": text_, "data": data_, "bss": bss_,
        "tsk": int(tsk.group(1)) if tsk else None,
        "idle": int(idle.group(1)) if idle else None,
        "mtime": datetime.datetime.fromtimestamp(os.path.getmtime(f"{d}/config.h")).strftime("%F %H:%M"),
    })

head = subprocess.run(["git", "-C", KLM, "log", "--oneline", "-1", "--format=%h %s"],
                      capture_output=True, text=True).stdout.strip()
WM = f"{TM}/workaround.marker"
workaround = open(WM).read().strip() if os.path.exists(WM) else None
data = {"mode": MODE, "generated": datetime.datetime.now().strftime("%F %H:%M"),
        "git_head": head, "workaround": workaround, "nominal": nominal, "helps": helps, "rows": rows}
json.dump(data, open(f"{TM}/ws63_cmp_data.json", "w"), ensure_ascii=False, indent=2)

for row in rows:
    if row["ram_total"] is None:
        raise SystemExit(f"footprint 未解析到 RAM 合计: {row['tier']}")

from openpyxl import Workbook
from openpyxl.styles import Alignment, Border, Font, PatternFill, Side
from openpyxl.utils import get_column_letter

HDR_FILL = PatternFill("solid", fgColor="1F4E79")
HDR_FONT = Font(bold=True, color="FFFFFF", size=10)
WARN_FILL = PatternFill("solid", fgColor="FFC7CE")
WARN_FONT = Font(color="9C0006", size=10, bold=True)
OK_FILL = PatternFill("solid", fgColor="C6EFCE")
OK_FONT = Font(color="006100", size=10)
NA_FILL = PatternFill("solid", fgColor="D9D9D9")
NA_FONT = Font(color="595959", size=10)
BODY = Font(size=10)
THIN = Border(*[Side(style="thin", color="BFBFBF")] * 4)
CENTER = Alignment(horizontal="center", vertical="center", wrap_text=True)
LEFT = Alignment(horizontal="left", vertical="top", wrap_text=True)
RIGHT = Alignment(horizontal="right", vertical="center")
NUM = '#,##0'
DEV = '+#,##0;-#,##0;0'

wb = Workbook()

def put_sheet(title, headers, widths):
    ws = wb.create_sheet(title)
    for ci, h in enumerate(headers, 1):
        c = ws.cell(1, ci, h)
        c.fill, c.font, c.alignment, c.border = HDR_FILL, HDR_FONT, CENTER, THIN
    for i, w in enumerate(widths, 1):
        ws.column_dimensions[get_column_letter(i)].width = w
    ws.freeze_panes = "A2"
    return ws

def put(ws, ri, ci, v, num=None, align=None, font=None, fill=None):
    c = ws.cell(ri, ci, v)
    c.border, c.font = THIN, font or BODY
    c.alignment = align or (RIGHT if num else CENTER)
    if num:
        c.number_format = num
    if fill:
        c.fill = fill
    return c

# ---- Sheet1 挡位对比总览 ----
ws = put_sheet("挡位对比总览",
               ["ws63使用场景", "RAM 基线数据(B)", "RAM 最新数据(B)", "RAM 偏差(B)",
                "RAM 判定", "ROM 基线数据", "ROM 最新数据(B)", "ROM 口径", "ROM 判定"],
               [20, 15, 13, 12, 10, 22, 13, 13, 20, 12])
for i, row in enumerate(rows, 2):
    t = row["tier"]
    n, a = nominal[t], row["ram_total"]
    warn = a > n
    put(ws, i, 1, TIER_LABEL[t])
    put(ws, i, 2, n, NUM)
    put(ws, i, 3, a, NUM, font=WARN_FONT if warn else BODY, fill=WARN_FILL if warn else None)
    put(ws, i, 4, a - n, DEV, font=WARN_FONT if warn else BODY, fill=WARN_FILL if warn else None)
    put(ws, i, 5, "⚠ warning 超出标称" if warn else "符合",
        font=WARN_FONT if warn else OK_FONT, fill=WARN_FILL if warn else OK_FILL)
    put(ws, i, 6, "—(未定义)", font=NA_FONT)
    put(ws, i, 7, row["rom"], NUM)
    put(ws, i, 8, row["rom_src"], align=CENTER)
    put(ws, i, 9, "N/A", font=NA_FONT, fill=NA_FILL)

# ---- Sheet2 RAM 构成与口径校准 ----
ws = put_sheet("RAM构成与口径校准",
               ["ws63使用场景", "静态符号(B)", "栈(B)", "动态申请(B)", "节点开销(B)", "RAM 最新数据(B)",
                "校准口径 静态+动态(B)", "基线数据(B)", "校准倍率", "校准判定"],
               [20, 13, 10, 13, 13, 13, 19, 13, 10, 22])
for i, row in enumerate(rows, 2):
    t = row["tier"]
    cp = row["comp"]
    st, sk, dy = cp.get("ram_static"), cp.get("stack"), cp.get("dyn")
    n = nominal[t]
    put(ws, i, 1, TIER_LABEL[t])
    put(ws, i, 2, st if st is not None else "?", NUM if st is not None else None)
    put(ws, i, 3, sk if sk is not None else "?", NUM if sk is not None else None)
    put(ws, i, 4, dy if dy is not None else "?", NUM if dy is not None else None)
    put(ws, i, 5, cp.get("overhead", "?"), NUM if cp.get("overhead") is not None else None)
    put(ws, i, 6, row["ram_total"], NUM)
    if st is not None and dy is not None:
        cal = st + dy
        warn = cal > n
        put(ws, i, 7, cal, NUM, font=WARN_FONT if warn else BODY, fill=WARN_FILL if warn else None)
        put(ws, i, 8, n, NUM)
        put(ws, i, 9, f"{cal / n:.1f}×", font=WARN_FONT if warn else BODY, fill=WARN_FILL if warn else None)
        put(ws, i, 10, "⚠ warning 超出标称" if warn else "符合",
            font=WARN_FONT if warn else OK_FONT, fill=WARN_FILL if warn else OK_FILL)
    else:
        put(ws, i, 7, "?"); put(ws, i, 8, n, NUM); put(ws, i, 9, "?"); put(ws, i, 10, "N/A", font=NA_FONT, fill=NA_FILL)
r = len(rows) + 2
put(ws, r, 1, "size 参考", font=NA_FONT)
for i, row in enumerate(rows, 2):
    put(ws, r, i, f"{row['tier'].upper()}: text={row['text']} data={row['data']} bss={row['bss']}",
        align=LEFT, font=NA_FONT)

# ---- Sheet3 挡位落地核验 ----
ws = put_sheet("挡位落地核验",
               ["ws63使用场景", "TSK_LIMIT 基线数据", "TSK_LIMIT 最新数据", "IDLE_STACK 预期(B)",
                "IDLE_STACK 最新数据(B)", "判定"],
               [20, 15, 15, 18, 18, 14])
for i, row in enumerate(rows, 2):
    t = row["tier"]
    et, ei = LAND[t]
    gt, gi = row["tsk"], row["idle"]
    ok = (gt == et) and (gi == ei)
    put(ws, i, 1, TIER_LABEL[t])
    put(ws, i, 2, et); put(ws, i, 3, gt if gt is not None else "?")
    put(ws, i, 4, ei, NUM); put(ws, i, 5, gi if gi is not None else "?", NUM)
    put(ws, i, 6, "一致" if ok else "⚠ 偏差",
        font=OK_FONT if ok else WARN_FONT, fill=OK_FILL if ok else WARN_FILL)

# ---- Sheet4 测量说明 ----
ws = put_sheet("测量说明", ["项", "内容"], [22, 100])
notes = [
    ("生成时间", f"{data['generated']}  (数据来源: {'复用 tier_meas 现场归档' if MODE == 'reuse' else '本脚本现场构建'})"),
    ("源码基线", head),
    ("测量产物时间", " / ".join(f"{r['tier'].upper()}={r['mtime']}" for r in rows)),
    ("预期值来源", "kernel/liteos_m/kernel/Kconfig 挡位 help 标称(脚本动态解析, 板无关):"),
] + [(f"  {t.upper()} 说明原文", helps[t]) for t in TIERS] + [
    ("RAM 实测方法", "tools/footprint/footprint.py 内核域口径: 静态符号+栈+动态申请+节点开销(堆池不计入); "
                   "命令: python3 footprint.py --out tier_meas/ws63_<挡位> --output .../fp"),
    ("ROM 实测方法", "gen_flash_size.py(与本脚本同目录): nm -S 符号级 text+rodata(逐 .o 扫描+map 汇编兜底), "
                   "flash 512KB, 不含 .data; 须在活产物(含 obj/)上运行, 复用模式取构建期产物"),
    ("ROM 无标称说明", "实现与挡位说明均未定义 ROM 标称 → 不作符合性判定(判定列 N/A)"),
    ("测量口径", "挡位纯净内核镜像: itp002 挡位 config 仅保留平台/架构/挡位行, KERNEL_TEST=n + NET_LWIP=n, "
               "非挡位管理组件交回 Kconfig 默认(含 TINY 下 IPC 全关/MUX=n, 符合挡位说明 §7.3; ws63 printf 走内核"
               " los_printf, 无 musl stdio 依赖, 不需 qemu 线的 MUX 拐杖)"),
    ("仅测量守卫", "los_init.c g_swtmrExpiryBuf 条件接管(INITCALL 期间不分配) + 板级 target_config.h "
                "SWTMR 硬编码中和(缺陷在位时), 构建后还原并 diff 核验"),
    ("warning 判定规则", "RAM 实测(footprint 内核域合计) > 挡位标称 → ⚠ warning; "
                      "口径校准(静态+动态) > 标称 → ⚠ warning; ROM 无标称不判定"),
]
if data.get("workaround"):
    notes.append(("⚠ 规避备注(守卫生效)", data["workaround"]))
for ri, (k, v) in enumerate(notes, 2):
    put(ws, ri, 1, k, align=LEFT, font=Font(size=10, bold=True))
    put(ws, ri, 2, v, align=LEFT)

del wb["Sheet"]
wb.save(XLSX)

print("=" * 60)
print("ws63 三挡位 RAM/ROM 预期 vs 实测(超出标称 ⚠ warning)")
for row in rows:
    t = row["tier"]
    n, a = nominal[t], row["ram_total"]
    mark = "⚠ warning" if a > n else "符合"
    print(f"  {t.upper():<9} RAM {n:>6} → {a:>6} B ({a - n:+5}, {a / n:.1f}×) {mark}"
          f" | ROM {row['rom']:>6} B [{row['rom_src']}] N/A")
print("=" * 60)
if data.get("workaround"):
    print(f"规避备注(仅测量守卫生效, 数据为守卫口径实测): {data['workaround']}")
print(f"Excel: {XLSX}")
PYEOF

log "全流程完成: $XLSX"
