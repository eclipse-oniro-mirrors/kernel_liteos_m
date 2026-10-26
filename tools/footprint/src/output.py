#!/usr/bin/env python3
# Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of
#    conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
#    of conditions and the following disclaimer in the documentation and/or other materials
#    provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may be used
#    to endorse or promote products derived from this software without specific prior written
#    permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Report writers: Markdown and Excel (xlsx via stdlib zip/XML)."""

import json
import os
import re
import zipfile
from datetime import datetime
from input import REPO_ROOT, fmt_kb
from scope import (RAM_SYMBOL_CLASSES, SEC_DESC,
                   SEC_LABELS, build_summary)


def md_table(header, rows):
    out = ["| " + " | ".join(header) + " |",
           "|" + "|".join(["---"] * len(header)) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(x) for x in r) + " |")
    return "\n".join(out)


def summary_lines(s):
    """Shared RAM summary lines for the report and stdout (single source)."""
    return (
        "RAM 构成: 静态符号 %d + 栈 %d + 动态申请 %d (+ 开销 %d); "
        "堆池 %d B 不计入 (共享基础设施, 见内存池布局)"
        % (s.ram_static, s.stack, s.dyn, s.overhead, s.heap),
        "RAM 合计 (不含节点开销): %d B (%.2f KB)"
        % (s.ram_total, fmt_kb(s.ram_total)),
        "RAM 合计 (含节点开销): %d B (%.2f KB)"
        % (s.ram_total_overhead, fmt_kb(s.ram_total_overhead)),
    )


def unresolved_note(items):
    """Suffix marking totals that exclude unresolved dynamic items."""
    n = len([it for it in items
             if it.size is None
             and it.status in ("dynamic", "source-static")
             and not it.transient])
    return " (不含 %d 项未解析动态项)" % n if n else ""


def _summary_rows(s, groups, scope, md=False):
    """Shared 汇总 table rows for the md and xlsx writers (D7).
    md mode marks unresolved dynamic sizes with '?' placeholders."""
    na = "?" if md else None
    rows = []
    for cls, label in SEC_LABELS:
        v = sum(g[cls] for g in groups.values())
        rows.append([label, SEC_DESC.get(label, ""), v, fmt_kb(v), "RAM"])
    rows.append(["栈 (.stack*/_stack)", "系统栈 (链接脚本段)", s.stack,
                 fmt_kb(s.stack), "RAM"])
    rows.append(["堆 (.heap)",
                 "堆池 (共享基础设施, 不计入 RAM 合计, 见内存池布局)",
                 s.heap, fmt_kb(s.heap), "不计入"])
    for it in s.persistent:
        rows.append(["动态-%s" % it.item, it.note,
                     it.size if it.size is not None else na,
                     fmt_kb(it.size) if it.size is not None else na, "RAM"])
    for it in s.map_static:
        rows.append(["静态-%s" % it.item,
                     "%s (已计入 .bss/.data%s)"
                     % (it.note, ", 不重复相加" if md else ""),
                     it.size, fmt_kb(it.size), "RAM"])
    rows.append(["节点头+对齐开销 (估)",
                 "逐堆节点 align_up(size,%d)−size+%dB 节点头 (按配置推导)"
                 % (scope.get("node_align", 8),
                    scope.get("node_header_bytes", 8)),
                 s.overhead, fmt_kb(s.overhead), "RAM"])
    return rows


XLSX_STYLES = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/'
    '2006/main">'
    '<fonts count="1"><font><sz val="11"/><name val="Calibri"/></font></fonts>'
    '<fills count="2"><fill><patternFill patternType="none"/></fill>'
    '<fill><patternFill patternType="gray125"/></fill></fills>'
    '<borders count="1"><border/></borders>'
    '<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" '
    'borderId="0"/></cellStyleXfs>'
    '<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" '
    'xfId="0"/></cellXfs>'
    '</styleSheet>'
)


def _xml_escape(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;")
             .replace(">", "&gt;").replace('"', "&quot;"))


def _col_letter(idx):
    s = ""
    while idx:
        idx, r = divmod(idx - 1, 26)
        s = chr(65 + r) + s
    return s


def _xlsx_cell(ref, v):
    if v is None or v == "":
        return ""
    if isinstance(v, bool):
        return '<c r="%s" t="b"><v>%d</v></c>' % (ref, 1 if v else 0)
    if isinstance(v, (int, float)):
        return '<c r="%s"><v>%s</v></c>' % (ref, v)
    return ('<c r="%s" t="inlineStr"><is><t xml:space="preserve">%s</t>'
            '</is></c>' % (ref, _xml_escape(str(v))))


def _xlsx_unique_name(name, i, used):
    n = re.sub(r"[\[\]:*?/\\]", "_", str(name))[:31] or "Sheet%d" % (i + 1)
    k = 1
    while n in used:
        suf = "~%d" % k
        n = n[:31 - len(suf)] + suf
        k += 1
    used.add(n)
    return n


def _xlsx_content_types(nsheets):
    ct = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
          '<Types xmlns="http://schemas.openxmlformats.org/package/2006/'
          'content-types">',
          '<Default Extension="rels" ContentType="application/'
          'vnd.openxmlformats-package.relationships+xml"/>',
          '<Default Extension="xml" ContentType="application/xml"/>',
          '<Override PartName="/xl/workbook.xml" ContentType="application/'
          'vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>',
          '<Override PartName="/xl/styles.xml" ContentType="application/'
          'vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>']
    for i in range(nsheets):
        ct.append('<Override PartName="/xl/worksheets/sheet%d.xml" '
                  'ContentType="application/vnd.openxmlformats-officedocument'
                  '.spreadsheetml.worksheet+xml"/>' % (i + 1))
    ct.append('</Types>')
    return ct


def _xlsx_workbook(names):
    wb = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
          '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/'
          '2006/main" xmlns:r="http://schemas.openxmlformats.org/'
          'officeDocument/2006/relationships"><sheets>']
    for i, nm in enumerate(names):
        wb.append('<sheet name="%s" sheetId="%d" r:id="rId%d"/>'
                  % (_xml_escape(nm), i + 1, i + 1))
    wb.append('</sheets></workbook>')
    return wb


def _xlsx_workbook_rels(names):
    rels = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/'
            '2006/relationships">']
    for i, _nm in enumerate(names):
        rels.append('<Relationship Id="rId%d" Type="http://schemas.'
                    'openxmlformats.org/officeDocument/2006/relationships/'
                    'worksheet" Target="worksheets/sheet%d.xml"/>'
                    % (i + 1, i + 1))
    rels.append('<Relationship Id="rId%d" Type="http://schemas.'
                'openxmlformats.org/officeDocument/2006/relationships/'
                'styles" Target="styles.xml"/>' % (len(names) + 1))
    rels.append('</Relationships>')
    return rels


def _xlsx_sheet_xml(rows):
    sr = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
          '<worksheet xmlns="http://schemas.openxmlformats.org/'
          'spreadsheetml/2006/main"><sheetData>']
    for r, row in enumerate(rows, 1):
        cells = "".join(_xlsx_cell("%s%d" % (_col_letter(c), r), v)
                        for c, v in enumerate(row, 1))
        sr.append('<row r="%d">%s</row>' % (r, cells))
    sr.append("</sheetData></worksheet>")
    return "\n".join(sr)


def write_xlsx(path, sheets):
    """sheets: list of (name, rows); rows: list of cell-value lists."""
    used = set()
    names = [_xlsx_unique_name(nm, i, used)
             for i, (nm, _r) in enumerate(sheets)]
    root_rels = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/'
        '2006/relationships"><Relationship Id="rId1" Type="http://schemas.'
        'openxmlformats.org/officeDocument/2006/relationships/officeDocument"'
        ' Target="xl/workbook.xml"/></Relationships>')
    parts = {
        "[Content_Types].xml": "\n".join(_xlsx_content_types(len(sheets))),
        "_rels/.rels": root_rels,
        "xl/workbook.xml": "\n".join(_xlsx_workbook(names)),
        "xl/_rels/workbook.xml.rels": "\n".join(_xlsx_workbook_rels(names)),
        "xl/styles.xml": XLSX_STYLES,
    }
    for i, (_nm, rows) in enumerate(sheets):
        parts["xl/worksheets/sheet%d.xml" % (i + 1)] = _xlsx_sheet_xml(rows)
    with zipfile.ZipFile(str(path), "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in parts.items():
            z.writestr(name, data)


def _sheet_summary_rows(map_path, config_path, s, groups, scope):
    rows = [["LiteOS-M 内核 RAM 统计"],
            ["map", str(map_path)], ["config", str(config_path)], [],
            ["段类别", "说明", "占用(B)", "占用(KB)", "类别"]]
    rows += _summary_rows(s, groups, scope)
    rows += [[], ["RAM 合计 (不含节点开销, B)", s.ram_total],
              ["RAM 合计 (含节点开销, B)", s.ram_total_overhead]]
    return rows


def _sheet_ram_symbols(symbols):
    hdr = ["组", "成员(.o)", "符号", "输入段", "输出段", "分类", "尺寸(B)"]
    ram = [s for s in symbols
           if set(s["classes"]) & set(RAM_SYMBOL_CLASSES)]
    return [hdr] + [[s["group"], s["member"], s["symbol"], s["isec"],
                     s["osec"], s["class"], s["size"]]
                    for s in sorted(ram, key=lambda x: -x["size"])]


def _sheet_dyn_items(items):
    return [["名称", "组", "状态", "尺寸(B)", "公式", "取值来源", "说明"]] + [
        [it.item, it.group, it.status,
         it.size if it.size is not None else None,
         it.expr, it.source, it.note] for it in items if not it.transient]


def _sheet_excluded(funnel):
    return [["名称", "RAM(B)", "原因"]] + [
        [n, e["ram"], e["reason"]] for n, e in funnel.excluded.items()]


def _sheet_unknown(funnel):
    return [["前缀", "RAM(B)"]] + [
        [n, u["ram"]] for n, u in funnel.unknown_kernel.items()]


def _sheet_sec_recon(sec_recon):
    return [["输出段", "分类", "段头(B)", "归属记录(B)", "fill(B)",
             "合并/relax(B)", "差额(B)", "备注"]] + [
        [r["name"], "+".join(r["classes"]), r["header"], r["attr"],
         r["fill"], r["relax"], r["diff"], r["note"]] for r in sec_recon]


def write_xlsx_report(outdir, map_path, config_path, groups, symbols, items,
                      funnel, outsec_sizes, scope, sec_recon=None):
    s = build_summary(groups, items, outsec_sizes, scope)
    sheets = [("汇总", _sheet_summary_rows(map_path, config_path,
                                           s, groups, scope)),
              ("RAM符号", _sheet_ram_symbols(symbols)),
              ("动态项", _sheet_dyn_items(items))]
    if funnel.excluded:
        sheets.append(("排除项", _sheet_excluded(funnel)))
    if funnel.unknown_kernel:
        sheets.append(("未识别成员", _sheet_unknown(funnel)))
    if sec_recon:
        sheets.append(("段对账", _sheet_sec_recon(sec_recon)))
    write_xlsx(outdir / "footprint.xlsx", sheets)


def _wr_header(ap, cfg_is_h, map_path, config_path):
    ap("# LiteOS-M 内核 RAM 统计报告")
    ap("")
    ap("- 生成时间: %s" % datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    ap("- map: `%s`" % map_path)
    ap("- config: `%s`%s" % (config_path,
                             " (auto-converted from config.h)"
                             if cfg_is_h else ""))
    ap("- 源码根: `%s` (工具所在仓库)" % REPO_ROOT)
    ap("")


def _wr_rules(ap, scope):
    ap("## 统计说明")
    ap("")
    ap("- 纳入: 内核库内 %d 组: %s"
       % (len(scope["group_prefixes"]),
          ", ".join(scope["group_prefixes"])))
    ap("- 排除: posix/cmsis/osal/bsp_config/liteos_port/musl/libgcc/"
        "产品库等 (明细见排除项表)")
    ap("- .data 计入 RAM 一次; .text_sram/.text_tcm 等 RAM 驻留代码计入 RAM")
    ap("- RAM 合计 = 静态符号 + 栈 + 动态申请; 堆池 (g_memStart 数组/"
       ".heap 段) 为内核/组件/产品共享基础设施, 不计入 RAM 合计, 见内存池布局")
    ap("- 栈按链接脚本保留段全计 (.stack* 前缀与 *_stack 后缀); "
       "段内多逻辑栈不拆分")
    ap("- 节点头按构建配置推导: %s = %d B (逐堆节点计头, "
       "AllocAlign 对齐垫随池态浮动不计)"
       % (scope.get("node_header_basis", "默认 TLSF 基础 8"),
          scope.get("node_header_bytes", 8)))
    ap("- 动态项数据来源 (括号内为代码定位 文件: 函数): 源码推算 = 扫描当前"
       "源码分配语句自动现算 (锚点可声明 probe_headers 引入模块私有头, "
       "尺寸宏与类型由编译探针实测); 登记公式 = 分配语句不在锚点函数内 "
       "(任务栈走创建通用管道/调用方传参) 或尺寸为运行时变量, 公式按源码"
       "人工登记; map 真值 = 链接 map 实测符号")
    ap("")


def _wr_summary(ap, s, groups, scope, items):
    ap("## 汇总")
    ap("")
    rows = _summary_rows(s, groups, scope, md=True)
    ap(md_table(["段类别", "说明", "占用 (B)", "占用 (KB)", "类别"], rows))
    ap("")
    unres_note = unresolved_note(items)
    comp, tot, tot_ov = summary_lines(s)
    ap("**%s**%s" % (tot, unres_note))
    ap("")
    ap("**%s**%s" % (tot_ov, unres_note))
    ap("")
    ap(comp)
    ap("")


def _wr_group_table(ap, groups):
    ap("## 子模块明细 (group x 段, B)")
    ap("")
    grows = []
    for g, row in groups.items():
        grows.append([g] + [row[c] for c, _ in SEC_LABELS] + [row["total"]])
    grows.append(["合计"] + [sum(g[c] for g in groups.values())
                             for c, _ in SEC_LABELS] +
                 [sum(g["total"] for g in groups.values())])
    ap(md_table(["group"] + [l for _, l in SEC_LABELS] + ["合计(B)"], grows))
    ap("")


def _wr_dyn_table(ap, items, disabled):
    ap("## 动态申请明细")
    ap("")
    drows = []
    for it in items:
        if it.transient:
            continue
        # 尺寸列占位: disabled 为"不适用"(-); ? 仅保留给未解析项
        # (求值失败/unknown), 让问号成为专属报警符号
        na = "-" if it.status == "disabled" else "?"
        drows.append([it.item, it.size if it.size is not None else na,
                      fmt_kb(it.size) if it.size else na,
                      it.status, it.expr or "", it.source, it.note])
    ap(md_table(["项", "大小(B)", "大小(KB)", "状态", "公式(现行源码)",
                 "数据来源", "备注"], drows))
    ap("")
    if disabled:
        ap("未启用项: %s" % ", ".join(
            "%s (%s)" % (it.item, it.source) for it in disabled))
        ap("")


def _cat_size(its):
    return sum(it.size for it in its
               if it.size is not None and not it.transient)


def _cat_names(its):
    return ", ".join(it.item for it in its) or "-"


def _wr_cat_overview(ap, items, disabled, unknown):
    # 动态项分类总览: 按取值来源归类计数 (源码扫描现算 / 登记公式 /
    # map 真值 / 配置未启用), 数据驱动, 各平台自动适配
    scan_items = [it for it in items if it.status == "dynamic"
                  and not it.transient
                  and it.source.startswith("源码推算")]
    reg_items = [it for it in items if it.status == "dynamic"
                 and not it.transient
                 and it.source.startswith("登记公式")]
    map_items = [it for it in items if it.status == "map-static"]
    cat_rows = [
        ["源码扫描现算", len(scan_items), _cat_size(scan_items),
         _cat_names(scan_items),
         "锚点函数内分配语句自动现算; 尺寸宏与类型由编译探针实测"],
        ["登记公式", len(reg_items), _cat_size(reg_items),
         _cat_names(reg_items),
         "分配语句不在锚点函数内 (任务栈走创建通用管道/调用方传参/"
         "模块内部常量), 公式按源码人工登记, 建议人工核验"],
        ["map 真值 (已计入静态合计)", len(map_items), "-",
         _cat_names(map_items),
         "本构建为静态数组形态, RAM 已在静态符号侧计, 不重复计入动态"],
        ["配置未启用", len(disabled), "-",
         _cat_names(disabled), "配置门未开, 本构建不分配"],
    ]
    if unknown:
        cat_rows.append(["求值失败/锚点失配", len(unknown), "-",
                         _cat_names(unknown),
                         "源码形态变化或取值未解析, 见告警区"])
    ap("### 动态项分类总览")
    ap("")
    ap(md_table(["分类", "数量", "尺寸合计(B)", "条目", "机制/说明"],
                cat_rows))
    ap("")


def _wr_pool_layout(ap, s, symbols, pool_carrier):
    ap("## 内存池布局 (sys 堆)")
    ap("")
    ap("堆池是内核/组件/产品共享的基础设施, 不计入 RAM 合计; "
       "上述动态申请各项即内核 init 从池内实际切走并常驻的部分。")
    ap("")
    if pool_carrier:
        exp = pool_carrier.get("expected")
        ap("- 载体: 内部堆 `g_memStart` 数组 %d B%s "
           "(已从静态符号剔除, 见排除项)"
           % (pool_carrier["size"],
              " (LOSCFG_SYS_HEAP_SIZE=%d)" % exp if exp is not None else ""))
    elif s.heap:
        ap("- 载体: `.heap` 链接段圈地 %d B (不计入 RAM, 段对账保留)" % s.heap)
    else:
        ap("- 载体: 自由尾巴 RAM (`__heap_start`/`__heap_size` 链接符号定界, "
           "段 0 字节, 动态申请即真实增量)")
    entries = sorted({s["symbol"] for s in symbols
                      if s["symbol"] in ("m_aucSysMem0", "m_aucSysMem1")})
    if entries:
        ap("- 堆入口符号: %s (`OS_SYS_MEM_ADDR` = LOSCFG_SYS_HEAP_ADDR, "
           "动态任务栈 `OS_TASK_STACK_ADDR` 同源)"
           % ", ".join("`%s`" % e for e in entries))
    ap("")


def _wr_exclusions(ap, funnel):
    ap("## 排除项 (供参考)")
    ap("")
    xrows = [[name, e["ram"], e["reason"] or ""]
             for name, e in sorted(funnel.excluded.items(),
                                    key=lambda kv: -kv[1]["ram"])
             if e["ram"]]
    xrows.append(["合计", sum(e["ram"] for e in funnel.excluded.values()),
                  ""])
    ap(md_table(["排除项", "RAM (B)", "理由"], xrows))
    ap("")


def _wr_sec_recon(ap, sec_recon):
    if sec_recon:
        ap("## 段对账 (链接器维度)")
        ap("")
        ap("差额 = 段头 − 归属记录 − fill；正值多为对齐/链接器填充，"
           "负值或大正值提示归属缺口。合并/relax 列是字符串合并去重"
           "扣减的归属字节（已从归属记录中扣除）。")
        ap("")
        rrows = [[r["name"], "+".join(r["classes"]), r["header"], r["attr"],
                  r["fill"], r["relax"], r["diff"], r["note"]]
                 for r in sec_recon]
        ap(md_table(["输出段", "分类", "段头 (B)", "归属记录 (B)", "fill (B)",
                     "合并/relax (B)", "差额 (B)", "备注"], rrows))
        ap("")


def _wr_warnings(ap, funnel, unknown, warnings):
    ap("## 告警")
    ap("")
    if funnel.unknown_kernel:
        for pfx, v in sorted(funnel.unknown_kernel.items(),
                              key=lambda kv: -kv[1]["ram"]):
            ap("- 未识别内核成员前缀 `%s.`: RAM %d B "
               "(内核库内但未纳入分组统计, 请确认是否需要补充)"
               % (pfx, v["ram"]))
    if unknown:
        for it in unknown:
            if not any(it.item in w for w in warnings):
                ap("- 动态项 %s 无法推算: %s" % (it.item, it.warn or it.source))
    if warnings:
        for w in warnings:
            ap("- %s" % w)
    if not (funnel.unknown_kernel or warnings or unknown):
        ap("- (无)")
    ap("")


def _wr_inputs_footer(ap, macros, board_info, probe_values, sizeof_table,
                      map_mtime):
    ap("## 输入与一致性")
    ap("")
    ap("- .config 宏: %d 个已定义" % len(macros))
    ap("- 板级配置: %s" % board_info)
    ap("- sizeof 来源: %s" % (
        "probe 编译实测" if probe_values else
        ("内部注入" if sizeof_table else "无")))
    if map_mtime:
        ap("- map 修改时间: %s" % datetime.fromtimestamp(map_mtime))
    ap("")


def write_report(outdir, cfg_is_h, map_path, config_path, macros, scope,
                 funnel, groups, symbols, items, outsec_sizes, probe_values,
                 sizeof_table, warnings, map_mtime, board_info="",
                 sec_recon=None, pool_carrier=None):
    s = build_summary(groups, items, outsec_sizes, scope)
    # 瞬态项 (调用期临时存在) 不在报告中体现 (D23): 明细/总览/告警/未启用
    # 均只呈现计入与门控信息, 锚点表仍全量跟踪
    disabled = [it for it in items
                if it.status == "disabled" and not it.transient]
    unknown = [it for it in items
               if it.status == "unknown" and not it.transient]
    lines = []
    ap = lines.append
    _wr_header(ap, cfg_is_h, map_path, config_path)
    _wr_rules(ap, scope)
    _wr_summary(ap, s, groups, scope, items)
    _wr_group_table(ap, groups)
    _wr_dyn_table(ap, items, disabled)
    _wr_cat_overview(ap, items, disabled, unknown)
    _wr_pool_layout(ap, s, symbols, pool_carrier)
    _wr_exclusions(ap, funnel)
    _wr_sec_recon(ap, sec_recon)
    _wr_warnings(ap, funnel, unknown, warnings)
    _wr_inputs_footer(ap, macros, board_info, probe_values, sizeof_table,
                      map_mtime)
    text = "\n".join(lines)
    with open(outdir / "report.md", "w", encoding="utf-8") as f:
        f.write(text)


def write_summary(outdir, s, groups, map_path, config_path,
                  map_mtime, config_mtime):
    """Machine-readable totals sidecar (summary.json).

    Pure numbers + input provenance only -- no timestamps, sorted keys:
    reruns on identical inputs produce byte-identical files.  Serves as
    the delta source for rerun comparisons and as CI gate input.
    """
    data = {
        "inputs": {
            "map": str(map_path), "map_mtime": map_mtime,
            "config": str(config_path), "config_mtime": config_mtime,
        },
        "totals": {
            "ram_static": s.ram_static,
            "stack": s.stack, "heap": s.heap, "dyn": s.dyn,
            "overhead": s.overhead, "ram_total": s.ram_total,
            "ram_total_overhead": s.ram_total_overhead,
        },
        "groups": {},
    }
    for name, row in groups.items():
        data["groups"][name] = {
            "ram": row["ram_data"] + row["ram_bss"] + row["ram_run"],
        }
    with open(os.path.join(str(outdir), "summary.json"), "w",
              encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")


def load_summary(outdir):
    """Previous run's summary dict, or None on any failure.

    The delta is an advisory feature: a missing or corrupt sidecar
    degrades to a skipped comparison, never to an error.
    """
    try:
        with open(os.path.join(str(outdir), "summary.json"),
                  encoding="utf-8") as f:
            return json.load(f)
    except (IOError, OSError, ValueError):
        return None
