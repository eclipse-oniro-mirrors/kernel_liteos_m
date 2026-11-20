#!/usr/bin/env python3
"""通用版固件 Flash 占用统计（nm ELF 直读，适用于任何单板产物目录）。

基于 gen_size_from_nm.py 改造，去除了所有硬编码路径和 Flash 容量，
自动探测产物目录、ELF 文件、map 文件，支持任意交叉编译工具链前缀。

原理:
  1. 遍历 obj/**/*.o，对每个 .o 跑 nm -S，建立 符号名→.o路径 映射表。
  2. 对最终 ELF 跑 nm -S，得到 GC 后实际链接的符号列表。
  3. nm 无 size 的符号（汇编 .o），用 map 里该 .o 的 section 大小兜底。
  4. 符号按 .o 路径前缀归类到模块，按类型(T/R)累加 text/rodata。

用法:
  # 自动探测（在产物目录或工程根目录下运行）
  python3 gen_flash_size.py

  # 指定产物目录（自动探测 ELF/map/obj）
  python3 gen_flash_size.py out/fm33le026_stk/fm33le026_demo

  # 完整指定
  python3 gen_flash_size.py \\
    --image out/fm33le026_stk/fm33le026_demo/OHOS_Image \\
    --map   out/fm33le026_stk/fm33le026_demo/OHOS_Image.map \\
    --obj-root out/fm33le026_stk/fm33le026_demo/obj \\
    --flash-size 131072 \\
    --nm arm-none-eabi-nm

  # 生成 Excel 报告
  python3 gen_flash_size.py --xlsx out/flash_usage.xlsx
"""
import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict

TYPE_TO_SECTION = {
    "T": "text", "t": "text",
    "R": "rodata", "r": "rodata",
}
FLASH_TYPES = set(TYPE_TO_SECTION.keys())


# ============================================================
# 自动探测
# ============================================================
def find_elf(start_dir):
    """在 start_dir 下查找 ELF 固件文件。"""
    candidates = ["OHOS_Image", "OHOS_Image.elf", "firmware.elf", "firmware.bin"]
    for name in candidates:
        p = os.path.join(start_dir, name)
        if os.path.isfile(p):
            return p
    # 模糊查找 *.elf / OHOS_Image*
    for fn in os.listdir(start_dir):
        if fn.endswith(".elf") or (fn.startswith("OHOS_Image") and ".bin" not in fn):
            return os.path.join(start_dir, fn)
    return None


def find_map(start_dir):
    """在 start_dir 下查找 map 文件。"""
    candidates = ["OHOS_Image.map", "firmware.map"]
    for name in candidates:
        p = os.path.join(start_dir, name)
        if os.path.isfile(p):
            return p
    for fn in os.listdir(start_dir):
        if fn.endswith(".map"):
            return os.path.join(start_dir, fn)
    return None


def find_obj_root(start_dir):
    """查找 obj 目录。"""
    obj_dir = os.path.join(start_dir, "obj")
    if os.path.isdir(obj_dir):
        return obj_dir
    return None


def auto_detect(out_dir):
    """自动探测产物目录下的 ELF/map/obj。"""
    out_dir = os.path.abspath(out_dir)
    image = find_elf(out_dir)
    map_file = find_map(out_dir)
    obj_root = find_obj_root(out_dir)
    return image, map_file, obj_root


# ============================================================
# nm 解析
# ============================================================
def run_nm(nm_cmd, path):
    try:
        out = subprocess.check_output(
            nm_cmd + ["-S", "--radix=d", path],
            text=True, stderr=subprocess.DEVNULL
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    syms = []
    for ln in out.splitlines():
        parts = ln.split()
        if len(parts) >= 4 and re.match(r"^\d+$", parts[0]) and re.match(r"^\d+$", parts[1]):
            syms.append((parts[2], int(parts[1]), parts[3]))
        elif len(parts) >= 3 and re.match(r"^\d+$", parts[0]) and parts[1] in FLASH_TYPES:
            syms.append((parts[1], 0, parts[2]))
        elif len(parts) >= 2 and parts[0] in FLASH_TYPES:
            syms.append((parts[0], 0, parts[1]))
    return syms


def build_symbol_map(nm_cmd, obj_root):
    sym_to_obj = {}
    obj_count = 0
    for dirpath, _, files in os.walk(obj_root):
        for fn in files:
            if not fn.endswith(".o"):
                continue
            obj_abs = os.path.join(dirpath, fn)
            obj_rel = os.path.relpath(obj_abs, obj_root)
            for typ, sz, name in run_nm(nm_cmd, obj_abs):
                if name and typ in FLASH_TYPES:
                    sym_to_obj[name] = obj_rel
            obj_count += 1
    return sym_to_obj, obj_count


# ============================================================
# 模块归类
# ============================================================
def classify_module(obj_rel, kernel_prefix):
    if obj_rel.startswith(kernel_prefix):
        return "kernel", _kernel_submodule(obj_rel, kernel_prefix)
    if "liteos_m/" in obj_rel and obj_rel.startswith("device/board/"):
        return "board", "board/src"
    if obj_rel.startswith("third_party/musl/"):
        return "musl", "musl/libc"
    if obj_rel.startswith("third_party/bounds_checking_function/"):
        return "securec", "securec/libsec"
    if obj_rel.startswith("third_party/cmsis/"):
        return "cmsis", "cmsis/core"
    if obj_rel.startswith("third_party/mbedtls/"):
        return "mbedtls", "mbedtls/crypto"
    if obj_rel.startswith("device/soc/"):
        return "soc", "soc/driver"
    if obj_rel.startswith("device/board/"):
        return "board", "board/other"
    if obj_rel.startswith("libs/"):
        return "ohos", "ohos/subsys"
    return "other", "other"


def _kernel_submodule(obj_rel, kernel_prefix):
    tail = obj_rel[len(kernel_prefix):]
    if tail.startswith("arch/"):
        return "arch"
    if tail.startswith("compat/"):
        return "compat/cmsis" if "cmsis" in tail else "compat/other"
    if tail.startswith("drivers/"):
        return "drivers"
    if tail.startswith("kernel/base/"):
        m = re.search(r"kernel/base/(mm/[^/]+|sched/[^/]+|[^/]+)/", tail)
        if m:
            return "kernel/" + m.group(1).replace("/", "_")
        return "kernel/base_other"
    if tail.startswith("kernel/extended/"):
        return "kernel/extended"
    if tail.startswith("kernel/src/"):
        return "kernel/kernel"
    if tail.startswith("lib/posix/"):
        return "lib/posix"
    if tail.startswith("lib/"):
        return "lib/other"
    return "kernel/other"


# ============================================================
# map 兜底
# ============================================================
def parse_map_obj_flash_sizes(map_path):
    obj_sizes = {}
    sec_re = re.compile(
        r"^\s+\.(text|rodata)\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)\s+(obj/\S+\.o)"
    )
    with open(map_path, "r", errors="replace") as f:
        for ln in f:
            m = sec_re.match(ln)
            if m:
                section, sz_hex, obj = m.group(1), m.group(2), m.group(3)
                sz = int(sz_hex, 16)
                if sz > 0:
                    key = (obj, section)
                    if sz > obj_sizes.get(key, 0):
                        obj_sizes[key] = sz
    return obj_sizes


# ============================================================
# 主统计
# ============================================================
def compute_stats(nm_cmd, image_path, obj_root, kernel_prefix, map_path=None):
    sym_to_obj, obj_count = build_symbol_map(nm_cmd, obj_root)
    image_syms = run_nm(nm_cmd, image_path)
    map_obj_sizes = parse_map_obj_flash_sizes(map_path) if map_path else {}

    nm_with_size = []
    nm_without_size = []
    for typ, sz, name in image_syms:
        if typ not in FLASH_TYPES:
            continue
        if sz > 0:
            nm_with_size.append((typ, sz, name))
        else:
            nm_without_size.append((typ, name))

    obj_nosize_syms = defaultdict(list)
    for typ, name in nm_without_size:
        obj_rel = sym_to_obj.get(name, "")
        if obj_rel:
            obj_nosize_syms[obj_rel].append((typ, name))

    fallback_syms = []
    for obj_rel, syms in obj_nosize_syms.items():
        obj_path = "obj/" + obj_rel
        total = 0
        for section in ("text", "rodata"):
            total += map_obj_sizes.get((obj_path, section), 0)
        if total > 0:
            fallback_syms.append((syms[0][0], total, obj_rel + " [asm .o fallback]"))

    all_flash_syms = nm_with_size + fallback_syms

    kernel_syms = []
    board_syms = []
    unknown_syms = []
    obj_totals = defaultdict(int)
    sub_totals = defaultdict(int)
    cat_totals = defaultdict(int)

    for typ, sz, name in all_flash_syms:
        section = TYPE_TO_SECTION[typ]
        if "[asm .o fallback]" in name:
            obj_rel = name.replace(" [asm .o fallback]", "")
        else:
            obj_rel = sym_to_obj.get(name, "")

        if not obj_rel:
            unknown_syms.append((section, name, sz))
            cat_totals["unknown"] += sz
            continue

        cat, sub = classify_module(obj_rel, kernel_prefix)
        cat_totals[cat] += sz
        obj_totals[obj_rel] += sz
        sub_totals[sub] += sz

        if cat == "kernel":
            kernel_syms.append((section, name, sz, obj_rel))
        elif cat == "board":
            board_syms.append((section, name, sz, obj_rel))

    return {
        "kernel_syms": kernel_syms,
        "board_syms": board_syms,
        "kernel_total": sum(s[2] for s in kernel_syms),
        "board_total": sum(s[2] for s in board_syms),
        "obj_totals": obj_totals,
        "sub_totals": sub_totals,
        "cat_totals": cat_totals,
        "unknown_syms": unknown_syms,
        "obj_count": obj_count,
        "sym_map_count": len(sym_to_obj),
        "image_sym_count": len(image_syms),
        "kernel_prefix": kernel_prefix,
        "nm_with_size_count": len(nm_with_size),
        "fallback_count": len(fallback_syms),
    }


# ============================================================
# 文本输出
# ============================================================
def print_text(image_path, stats, flash_size):
    kt = stats["kernel_total"]
    bt = stats["board_total"]
    ks = stats["kernel_syms"]
    bs = stats["board_syms"]
    cat = stats["cat_totals"]
    obj_totals = stats["obj_totals"]
    sub_totals = stats["sub_totals"]

    print("=" * 72)
    print("固件 Flash 占用统计（nm ELF 直读，符号级精确）")
    print("=" * 72)
    print(f"ELF 文件            : {image_path}")
    print(f"符号→.o 映射表大小   : {stats['sym_map_count']} (扫描 {stats['obj_count']} 个 .o)")
    print(f"ELF 符号表条目数     : {stats['image_sym_count']}")
    print(f"nm 有 size 符号      : {stats.get('nm_with_size_count', 0)} 个")
    print(f"map 兜底 .o          : {stats.get('fallback_count', 0)} 个 (汇编 .o, nm 无 size)")
    print(f"Flash 分区总容量     : {flash_size} 字节 ({flash_size//1024}KB)")
    print()

    total_flash = sum(cat.values())
    print(f"Flash 总占用 (text+rodata): {total_flash} 字节 ({100*total_flash/flash_size:.1f}%)")
    print()
    print(f"{'类别':<14} {'字节':>8} {'占Flash%':>9}")
    print("-" * 35)
    for c in ["kernel", "board", "musl", "securec", "cmsis", "mbedtls", "soc", "ohos", "other", "unknown"]:
        if cat.get(c, 0) > 0:
            print(f"{c:<14} {cat[c]:>8} {100*cat[c]/flash_size:>8.1f}%")
    print("-" * 35)
    print(f"{'合计':<14} {total_flash:>8} {100*total_flash/flash_size:>8.1f}%")
    print()

    print("=" * 72)
    print(f"严格内核 Flash 占用: {kt} 字节 ({100*kt/flash_size:.2f}%)")
    print(f"板级适配 Flash 占用: {bt} 字节 ({100*bt/flash_size:.2f}%)")
    print(f"严格内核符号数      : {len(ks)} 个")
    if stats["unknown_syms"]:
        print(f"未归类符号数        : {len(stats['unknown_syms'])} 个 ({sum(s[2] for s in stats['unknown_syms'])} 字节)")
    print()

    print("=" * 72)
    print("严格内核 - 按子模块汇总")
    print("=" * 72)
    print(f"{'子模块':<26} {'字节':>8} {'占内核%':>8}")
    print("-" * 46)
    for sm in sorted(sub_totals.keys(), key=lambda x: -sub_totals[x]):
        if sm.startswith("kernel/") or sm in ("arch", "compat/cmsis", "compat/other", "drivers", "lib/posix", "lib/other"):
            sz = sub_totals[sm]
            print(f"{sm:<26} {sz:>8} {100*sz/kt:>7.1f}%" if kt > 0 else f"{sm:<26} {sz:>8}      -")
    print(f"{'合计':<26} {kt:>8} {'100.0%':>8}")
    print()

    print("=" * 72)
    print("严格内核 - 按对象文件汇总")
    print("=" * 72)
    kp = stats["kernel_prefix"]
    kernel_objs = {k: v for k, v in obj_totals.items() if k.startswith(kp)}
    print(f"{'字节':>6}  {'占内核%':>7}  对象文件")
    for obj, sz in sorted(kernel_objs.items(), key=lambda x: -x[1]):
        print(f"{sz:>6}  {100*sz/kt:>6.1f}%  {obj}" if kt > 0 else f"{sz:>6}        -  {obj}")
    print(f"{'合计':>6}  {'100.0%':>7}  ({len(kernel_objs)} 个对象, {len(ks)} 个符号)")
    print()

    print("=" * 72)
    print("严格内核 - 符号明细（按子模块+对象+字节降序）")
    print("=" * 72)
    print(f"{'字节':>6}  {'占内核%':>7}  {'Section':<10} {'子模块':<22} 符号名")
    for sec, sym, sz, obj in sorted(ks, key=lambda s: (_kernel_submodule(s[3], kp), s[3], -s[2])):
        sm = _kernel_submodule(obj, kp)
        print(f"{sz:>6}  {100*sz/kt:>6.2f}%  {sec:<10} {sm:<22} {sym}" if kt > 0 else f"{sz:>6}        -  {sec:<10} {sm:<22} {sym}")

    if bs:
        print()
        print("=" * 72)
        print("板级适配 - 符号明细")
        print("=" * 72)
        print(f"{'字节':>6}  {'Section':<10} {'对象':<70} 符号名")
        for sec, sym, sz, obj in sorted(bs, key=lambda s: (s[3], -s[2])):
            print(f"{sz:>6}  {sec:<10} {obj:<70} {sym}")


# ============================================================
# xlsx 输出
# ============================================================
def gen_xlsx(image_path, out_path, stats, flash_size):
    try:
        from openpyxl import Workbook
        from openpyxl.styles import Font, PatternFill, Alignment, Border, Side
    except ImportError:
        print("openpyxl 未安装，跳过 xlsx 生成", file=sys.stderr)
        return

    ks = stats["kernel_syms"]
    kt = stats["kernel_total"]
    cat = stats["cat_totals"]
    obj_totals = stats["obj_totals"]
    sub_totals = stats["sub_totals"]

    title_font = Font(name="Arial", size=14, bold=True, color="FFFFFF")
    title_fill = PatternFill("solid", fgColor="305496")
    header_font = Font(name="Arial", size=10, bold=True, color="FFFFFF")
    header_fill = PatternFill("solid", fgColor="4472C4")
    total_font = Font(name="Arial", size=11, bold=True)
    total_fill = PatternFill("solid", fgColor="FFE699")
    group_font = Font(name="Arial", size=10, bold=True)
    group_fill = PatternFill("solid", fgColor="D9E1F2")
    normal_font = Font(name="Arial", size=10)
    mono_font = Font(name="Consolas", size=10)
    center = Alignment(horizontal="center", vertical="center")
    left = Alignment(horizontal="left", vertical="center")
    right = Alignment(horizontal="right", vertical="center")
    thin = Side(style="thin", color="BFBFBF")
    border = Border(left=thin, right=thin, top=thin, bottom=thin)

    def style_header_row(ws, row, ncols):
        for col in range(1, ncols + 1):
            c = ws.cell(row=row, column=col)
            c.font = header_font; c.fill = header_fill
            c.alignment = center; c.border = border

    wb = Workbook()

    # Sheet 1: 总览
    ws1 = wb.active
    ws1.title = "Flash占用总览"
    ws1.merge_cells("A1:E1")
    c = ws1["A1"]
    c.value = "固件 Flash 占用统计（nm ELF 直读）"
    c.font = title_font; c.fill = title_fill; c.alignment = center
    ws1.row_dimensions[1].height = 28

    meta = [
        ("ELF 文件", image_path),
        ("符号映射表", f"{stats['sym_map_count']} 条 (扫描 {stats['obj_count']} 个 .o)"),
        ("ELF 符号表", f"{stats['image_sym_count']} 条"),
        ("Flash 总容量", f"{flash_size} 字节 ({flash_size//1024}KB)"),
    ]
    for i, (k, v) in enumerate(meta, start=3):
        ws1.cell(row=i, column=1, value=k).font = Font(name="Arial", size=10, bold=True)
        ws1.cell(row=i, column=2, value=v).font = normal_font

    r = 8
    ws1.cell(row=r, column=1, value="按类别汇总").font = Font(name="Arial", size=12, bold=True, color="305496")
    r += 1
    for i, h in enumerate(["类别", "字节", "占Flash%"], 1):
        ws1.cell(row=r, column=i, value=h)
    style_header_row(ws1, r, 3)
    r += 1
    total_flash = sum(cat.values())
    for c_name in ["kernel", "board", "musl", "securec", "cmsis", "mbedtls", "soc", "ohos", "other", "unknown"]:
        if cat.get(c_name, 0) > 0:
            ws1.cell(row=r, column=1, value=c_name).font = group_font
            ws1.cell(row=r, column=1).fill = group_fill
            ws1.cell(row=r, column=2, value=cat[c_name]).number_format = '#,##0'
            ws1.cell(row=r, column=3, value=cat[c_name]/flash_size).number_format = '0.00%'
            for col in range(1, 4):
                ws1.cell(row=r, column=col).border = border
                if col != 1: ws1.cell(row=r, column=col).font = normal_font
                ws1.cell(row=r, column=col).alignment = left if col == 1 else right
            r += 1
    ws1.cell(row=r, column=1, value="合计").font = total_font
    ws1.cell(row=r, column=1).fill = total_fill
    ws1.cell(row=r, column=2, value=total_flash).number_format = '#,##0'
    ws1.cell(row=r, column=3, value=total_flash/flash_size).number_format = '0.00%'
    for col in range(1, 4):
        ws1.cell(row=r, column=col).fill = total_fill
        ws1.cell(row=r, column=col).font = total_font
        ws1.cell(row=r, column=col).border = border
    ws1.column_dimensions["A"].width = 20
    ws1.column_dimensions["B"].width = 16
    ws1.column_dimensions["C"].width = 12

    # Sheet 2: 内核符号明细
    ws2 = wb.create_sheet("内核符号明细")
    ws2.merge_cells("A1:G1")
    c = ws2["A1"]
    c.value = "内核符号 Flash 占用明细（nm ELF 直读）"
    c.font = title_font; c.fill = title_fill; c.alignment = center
    ws2.row_dimensions[1].height = 28
    for i, h in enumerate(["子模块", "对象文件 (.o)", "Section", "符号名", "字节", "占内核%", "占对象%"], 1):
        ws2.cell(row=2, column=i, value=h)
    style_header_row(ws2, 2, 7)
    r = 3
    kp = stats["kernel_prefix"]
    for sec, sym, sz, obj in sorted(ks, key=lambda s: (_kernel_submodule(s[3], kp), s[3], -s[2])):
        sm = _kernel_submodule(obj, kp)
        ws2.cell(row=r, column=1, value=sm)
        ws2.cell(row=r, column=2, value=obj)
        ws2.cell(row=r, column=3, value=sec)
        ws2.cell(row=r, column=4, value=sym).font = mono_font
        ws2.cell(row=r, column=5, value=sz).number_format = '#,##0'
        ws2.cell(row=r, column=6, value=sz/kt if kt > 0 else 0).number_format = '0.00%'
        ws2.cell(row=r, column=7, value=sz/obj_totals[obj] if obj_totals.get(obj, 0) > 0 else 0).number_format = '0.0%'
        for col in range(1, 8):
            cell = ws2.cell(row=r, column=col)
            cell.border = border
            if col != 4: cell.font = normal_font
            cell.alignment = left if col in (1, 2, 4) else right
        r += 1
    ws2.cell(row=r, column=1, value="合计").font = total_font
    ws2.cell(row=r, column=1).fill = total_fill
    ws2.cell(row=r, column=5, value=kt).number_format = '#,##0'
    ws2.cell(row=r, column=5).font = total_font
    ws2.cell(row=r, column=5).fill = total_fill
    for col in range(1, 8):
        ws2.cell(row=r, column=col).fill = total_fill
        ws2.cell(row=r, column=col).border = border
    for col, w in [("A", 22), ("B", 70), ("C", 10), ("D", 40), ("E", 10), ("F", 10), ("G", 10)]:
        ws2.column_dimensions[col].width = w
    ws2.freeze_panes = "D3"

    # Sheet 3: 板级适配符号明细
    bs = stats["board_syms"]
    bt = stats["board_total"]
    if bs:
        ws3 = wb.create_sheet("板级适配符号明细")
        ws3.merge_cells("A1:G1")
        c = ws3["A1"]
        c.value = "板级适配 Flash 占用（非内核源码）"
        c.font = title_font; c.fill = title_fill; c.alignment = center
        ws3.row_dimensions[1].height = 28
        for i, h in enumerate(["子模块", "对象文件 (.o)", "Section", "符号名", "字节", "占板级%", "占对象%"], 1):
            ws3.cell(row=2, column=i, value=h)
        style_header_row(ws3, 2, 7)
        board_obj_totals = defaultdict(int)
        for sec, sym, sz, obj in bs:
            board_obj_totals[obj] += sz
        r = 3
        for sec, sym, sz, obj in sorted(bs, key=lambda s: (s[3], -s[2])):
            ws3.cell(row=r, column=1, value="board/src")
            ws3.cell(row=r, column=2, value=obj)
            ws3.cell(row=r, column=3, value=sec)
            ws3.cell(row=r, column=4, value=sym).font = mono_font
            ws3.cell(row=r, column=5, value=sz).number_format = '#,##0'
            ws3.cell(row=r, column=6, value=sz/bt if bt else 0).number_format = '0.00%'
            ws3.cell(row=r, column=7, value=sz/board_obj_totals[obj] if board_obj_totals.get(obj, 0) > 0 else 0).number_format = '0.0%'
            for col in range(1, 8):
                cell = ws3.cell(row=r, column=col)
                cell.border = border
                if col != 4: cell.font = normal_font
                cell.alignment = left if col in (1, 2, 4) else right
            r += 1
        ws3.cell(row=r, column=1, value="合计").font = total_font
        ws3.cell(row=r, column=1).fill = total_fill
        ws3.cell(row=r, column=5, value=bt).number_format = '#,##0'
        ws3.cell(row=r, column=5).font = total_font
        ws3.cell(row=r, column=5).fill = total_fill
        for col in range(1, 8):
            ws3.cell(row=r, column=col).fill = total_fill
            ws3.cell(row=r, column=col).border = border
        for col, w in [("A", 14), ("B", 70), ("C", 10), ("D", 40), ("E", 10), ("F", 10), ("G", 10)]:
            ws3.column_dimensions[col].width = w
        ws3.freeze_panes = "D3"

    wb.save(out_path)


# ============================================================
# JSON 输出（供 feishu_report.py 消费）
# ============================================================
def output_json(out_path, image_path, stats, flash_size, platform=""):
    import datetime
    import json

    kt = stats["kernel_total"]
    cat = stats["cat_totals"]
    obj_totals = stats["obj_totals"]
    sub_totals = stats["sub_totals"]
    ks = stats["kernel_syms"]
    kp = stats["kernel_prefix"]

    kernel_objs = {k: v for k, v in obj_totals.items() if k.startswith(kp)}
    top_objs = sorted(kernel_objs.items(), key=lambda x: -x[1])[:10]

    result = {
        "date": datetime.date.today().isoformat(),
        "timestamp": datetime.datetime.now().isoformat(),
        "platform": platform,
        "image": image_path,
        "flash_size": flash_size,
        "total_flash": sum(cat.values()),
        "categories": dict(cat),
        "kernel": {
            "total": kt,
            "symbol_count": len(ks),
            "sub_modules": {k: v for k, v in sorted(sub_totals.items(), key=lambda x: -x[1])
                            if k.startswith("kernel/") or k in ("arch", "compat/cmsis", "drivers", "lib/posix")},
            "top_objs": [{"obj": o, "bytes": s, "pct": round(100 * s / kt, 1) if kt > 0 else 0}
                         for o, s in top_objs],
            "all_objs": {o: s for o, s in kernel_objs.items()},
        },
    }
    with open(out_path, "w") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)


# ============================================================
# main
# ============================================================
def main():
    ap = argparse.ArgumentParser(
        description="通用版固件 Flash 占用统计（nm ELF 直读，适用于任何单板）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
示例:
  # 自动探测（在产物目录下运行）
  %(prog)s out/fm33le026_stk/fm33le026_demo

  # 在工程根目录下自动探测（指定产物目录）
  %(prog)s --out-dir out/fm33le026_stk/fm33le026_demo

  # 完整指定
  %(prog)s \\
    --image out/fm33le026_stk/fm33le026_demo/OHOS_Image \\
    --map   out/fm33le026_stk/fm33le026_demo/OHOS_Image.map \\
    --obj-root out/fm33le026_stk/fm33le026_demo/obj

  # 生成 Excel 报告
  %(prog)s --out-dir out/fm33le026_stk/fm33le026_demo --xlsx out/flash_usage.xlsx
""",
    )
    ap.add_argument("out_dir", nargs="?", default=None,
                    help="产物目录（自动探测 ELF/map/obj），如 out/fm33le026_stk/fm33le026_demo")
    ap.add_argument("--out-dir", dest="out_dir_opt", default=None,
                    help="同 out_dir 位置参数，产物目录")
    ap.add_argument("--image", default=None,
                    help="ELF 固件路径（不指定则自动探测）")
    ap.add_argument("--map", default=None,
                    help="map 文件路径（不指定则自动探测）")
    ap.add_argument("--obj-root", default=None,
                    help=".o 根目录（不指定则自动探测）")
    ap.add_argument("--kernel-prefix", default="kernel/liteos_m/",
                    help="严格内核 .o 路径前缀，相对 obj_root (默认: kernel/liteos_m/)")
    ap.add_argument("--flash-size", type=int, default=131072,
                    help="Flash 分区总容量，字节 (默认: 131072 = 128KB)")
    ap.add_argument("--nm", default="arm-none-eabi-nm",
                    help="nm 命令 (默认: arm-none-eabi-nm)")
    ap.add_argument("--xlsx", metavar="PATH", default=None,
                    help="生成 Excel 报告到该路径")
    ap.add_argument("--json", metavar="PATH", default=None,
                    help="输出 JSON 结果到该路径（供 feishu_report.py 消费）")
    args = ap.parse_args()

    # 确定 out_dir
    out_dir = args.out_dir or args.out_dir_opt
    if out_dir and not os.path.isdir(out_dir):
        print(f"错误: 产物目录不存在 {out_dir}", file=sys.stderr)
        sys.exit(1)

    # 自动探测或使用指定值
    image = args.image
    map_file = args.map
    obj_root = args.obj_root

    if out_dir and (not image or not map_file or not obj_root):
        a_image, a_map, a_obj = auto_detect(out_dir)
        if not image:
            image = a_image
        if not map_file:
            map_file = a_map
        if not obj_root:
            obj_root = a_obj

    if not image or not os.path.isfile(image):
        print("错误: 找不到 ELF 文件，请用 --image 指定或 --out-dir 指定产物目录", file=sys.stderr)
        sys.exit(1)
    if not obj_root or not os.path.isdir(obj_root):
        print("错误: 找不到 .o 根目录，请用 --obj-root 指定或 --out-dir 指定产物目录", file=sys.stderr)
        sys.exit(1)

    nm_cmd = args.nm.split()

    print(f"[INFO] ELF     : {image}", file=sys.stderr)
    print(f"[INFO] MAP     : {map_file or '(无, 跳过汇编兜底)'}", file=sys.stderr)
    print(f"[INFO] OBJ ROOT: {obj_root}", file=sys.stderr)
    print(f"[INFO] NM      : {nm_cmd[0]}", file=sys.stderr)
    print(f"[INFO] FLASH   : {args.flash_size} bytes ({args.flash_size//1024}KB)", file=sys.stderr)
    print(file=sys.stderr)

    stats = compute_stats(nm_cmd, image, obj_root, args.kernel_prefix, map_file)
    print_text(image, stats, args.flash_size)

    if args.xlsx:
        gen_xlsx(image, args.xlsx, stats, args.flash_size)
        print(f"\nExcel 报告已生成: {args.xlsx}", file=sys.stderr)

    if args.json:
        platform = os.path.basename(out_dir) if out_dir else ""
        output_json(args.json, image, stats, args.flash_size, platform)
        print(f"JSON 结果已生成: {args.json}", file=sys.stderr)


if __name__ == "__main__":
    main()
