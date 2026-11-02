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
"""Statistical scope and aggregation: classify, funnel, totals, reconcile."""

import re
from collections import OrderedDict, namedtuple
from anchors import node_overhead
from input import attr_basename, attr_prefix, rec_name

DEFAULT_SCOPE = {
    # layer 1: is the attribution from the kernel library / kernel repo?
    "kernel_lib_patterns": ["kernel/liteos_m", "libliteos_m.a("],
    # layer 2: member-name prefixes of the approved kernel scope
    # (拍板 8 组 + hpl 2026-10-27 审计转正 + trace/lms/power 2026-10-28
    #  审查转正, 均为 kernel/extended 内核特性/本体模块)
    "group_prefixes": [
        "base", "arch", "common", "cpup",
        "backtrace", "exchook", "signal", "interrupt", "hpl",
        # 2026-10-28 审查转正 (D24): kernel/extended 内核本体模块
        "trace", "lms", "power",
    ],
    # kernel-repo members that are explicitly out of scope (排除项)
    "excluded_prefixes": [
        "posix", "cmsis", "osal", "bsp_config", "liteos_port",
        "lwip", "net", "shell", "fs", "drivers",
    ],
    "excluded_path_segments": [
        "compat", "components", "lib", "shell", "fs", "testsuites",
        "targets", "drivers", "kal", "boot", "startup",
    ],
    # exclusion display reasons
    "exclude_reasons": {
        "posix": "POSIX 兼容层 (拍板排除)",
        "cmsis": "CMSIS 兼容层 (拍板排除)",
        "osal": "OSAL (内核之上, 拍板排除)",
        "bsp_config": "板级配置 tick/HAL (拍板排除)",
        "liteos_port": "移植胶水 (拍板排除)",
        "musl": "musl C 库",
        "libgcc": "编译器运行时",
        "lwip": "lwip 协议栈",
        "compat": "兼容层目录 (compat/)",
        "components": "内核仓非内核组件 (components/)",
        "lib": "内核仓库目录 (lib/)",
        "shell": "Shell",
        "fs": "文件系统",
        "testsuites": "测试套件",
        "targets": "目标板代码",
        "drivers": "驱动",
        "third_party": "三方库",
        "device": "产品/板级代码",
        "vendor": "产品代码",
    },
    # layer 3: output-section classification
    "section_class": {
        "text": ["rom_text"],
        "rodata": ["rom_rodata"],
        "data": ["ram_data"],
        "bss": ["ram_bss"],
        # riscv small data (-msmall-data-limit): same as data/bss
        "sdata": ["ram_data"],
        "sbss": ["ram_bss"],
        # relocation table: flash copy + loaded to RAM, like .data
        "got": ["ram_data"],
        # ram-resident code (flash copy + runs in RAM/sram/tcm): dual count
        "text_sram": ["rom_text", "ram_run"],
        "text_tcm": ["rom_text", "ram_run"],
        "data_tcm": ["ram_data"],
        "stack": ["stack"],
        "stacks": ["stack"],
        "heap": ["heap"],
        "init": ["rom_other"],
        "fini": ["rom_other"],
        "preinit_array": ["rom_other"],
        "init_array": ["rom_other"],
        "fini_array": ["rom_other"],
        "exidx": ["rom_other"],
        "extab": ["rom_other"],
        "sysintcall": ["rom_other"],
        "sysinitcall": ["rom_other"],
        "iplt": ["rom_other"],
        "rel": ["rom_other"],
        "vector": ["rom_other"],
        "vfp": ["ignore"],
        "glue": ["ignore"],
        "eh_frame": ["rom_other"],
        "comment": ["ignore"],
        "debug": ["ignore"],
        "note": ["ignore"],
        "arm": ["ignore"],
        "gnu": ["ignore"],
    },
    # deterministic node-header overhead estimate for dynamic allocations
    # (overridden per build by node_header_for(); the default 8 is the
    # base TLSF node, kept as the fail-safe when the config is unknown)
    "node_header_bytes": 8,
    "node_align": 8,
}

# default LOSCFG_MEM_RECORD_LR_CNT (kernel/include/los_config.h #ifndef)
_MEM_RECORD_LR_CNT_DEFAULT = 3


def _lr_cnt(macros):
    try:
        v = macros.get("LOSCFG_MEM_RECORD_LR_CNT")
        return int(v) if v is not None else _MEM_RECORD_LR_CNT_DEFAULT
    except (TypeError, ValueError):
        return _MEM_RECORD_LR_CNT_DEFAULT


def node_header_for(macros):
    """sizeof(node header) derived from the build's own config.

    Two allocator families, both struct-truth of the tree:
      TLSF     kernel/include/los_memory.h struct OsMemNodeHead:
               ptr + sizeAndFlag = 8, + magic (INTEGRITY_CHECK),
               + linkReg[LR_CNT] (LEAKCHECK), + taskId (TASK_MEM_USED;
               MEM_FREE_BY_TASKID is a bitfield and adds nothing)
      BESTFIT  kernel/base/mem/bestfit/los_memory_internal.h LosMemCtlNode:
               freeNodeInfo union + preNode + sizeAndFlag = 16,
               + gapSize+checksum (MEM_HEAD_BACKUP),
               + linkReg[LR_CNT] (LEAKCHECK)
    Returns (bytes, basis) where basis is the derivation echo for the
    report. an386 (TLSF+INTEGRITY+TASK_MEM_USED) = 16, verified against
    the QEMU runtime heap-node histogram (sizeof(OsMemNodeHead) = 16).
    bestfit+SLAB_EXTENTION boards serve <=128B allocations from 4B-header
    slab blocks instead -- the estimate still uses the main-pool node
    header, documented as an upper bound for such items.
    """

    def on(key):
        return macros.get(key) not in (None, 0)

    lr = _lr_cnt(macros)
    if on("LOSCFG_KERNEL_MEM_BESTFIT"):
        hdr = 16
        basis = ["BESTFIT LosMemCtlNode: 16"]
        if on("LOSCFG_MEM_HEAD_BACKUP"):
            hdr += 8
            basis.append("MEM_HEAD_BACKUP +8")
        if on("LOSCFG_MEM_LEAKCHECK"):
            hdr += 4 * lr
            basis.append("MEM_LEAKCHECK 4x%d" % lr)
        return hdr, "; ".join(basis)
    hdr = 8
    basis = ["TLSF OsMemNodeHead: 8"]
    if on("LOSCFG_BASE_MEM_NODE_INTEGRITY_CHECK"):
        hdr += 4
        basis.append("INTEGRITY_CHECK +4")
    if on("LOSCFG_MEM_LEAKCHECK"):
        hdr += 4 * lr
        basis.append("MEM_LEAKCHECK 4x%d" % lr)
    if on("LOSCFG_TASK_MEM_USED"):
        hdr += 4
        basis.append("TASK_MEM_USED +4")
    return hdr, "; ".join(basis)


REASON_RULES = [
    (r"test", "测试套件"),
    (r"musl", "musl C 库"),
    (r"libgcc", "编译器运行时"),
    (r"lwip", "lwip 协议栈"),
    (r"littlefs|lfs|fs", "文件系统"),
    (r"shell", "Shell"),
    (r"posix", "POSIX 兼容层 (拍板排除)"),
    (r"cmsis", "CMSIS 兼容层 (拍板排除)"),
    (r"bounds_check", "安全库 (securec)"),
    (r"^(device|vendor)/", "产品/板级代码"),
    (r"^third_party/", "三方库"),
]


def reason_for_name(name):
    for rx, reason in REASON_RULES:
        if re.search(rx, name):
            return reason
    return ""


def kernel_tail_segments(attr):
    """Path segments after 'kernel/liteos_m' in the attribution, if any."""
    norm = attr.replace("\\", "/")
    idx = norm.find("kernel/liteos_m/")
    if idx < 0:
        return []
    tail = norm[idx + len("kernel/liteos_m/"):]
    return [s for s in tail.split("/") if s]


def sec_classes(osec, scope, isec=None):
    """All classes a record contributes to (dual count possible).

    Classification is by output-section name; when the output section is
    unrecognized ("other") and an input-section name is given, fall back
    to it -- fbb-style linker scripts pack all program content into one
    container section (e.g. `.startup`) whose name carries no semantics,
    while the input names (.text/.rodata/...) still do.
    """
    if not osec:
        return ["other"]
    name = osec.lstrip(".")
    for key, classes in scope["section_class"].items():
        if name == key or name.startswith(key + "."):
            return classes
    # dotted compound names: .ARM.extab -> extab rule
    for key, classes in scope["section_class"].items():
        if name.endswith("." + key):
            return classes
    # linker-reserved stack/heap sections with descriptive prefixes
    # (D19: .nmi_stack / .irq_stack / *_heap -- same pool semantics)
    if name.endswith("_stack"):
        return ["stack"]
    if name.endswith("_heap"):
        return ["heap"]
    # non-allocated debug/stab families (debug_str, debug_info, stabstr...)
    # never load; record sizes there are pre-merge and meaningless
    for fam in ("debug", "stab"):
        if name == fam or name.startswith(fam + "_") or \
                name.startswith(fam + "."):
            return ["ignore"]
    # target attributes (ARM/riscv) never load
    if name.endswith(".attributes"):
        return ["ignore"]
    # container output section: the input section name carries semantics
    if isec and isec != osec:
        return sec_classes(isec, scope)
    return ["other"]


class FunnelResult:

    def __init__(self):
        self.kernel = []           # MapRec list inside approved scope
        self.excluded = OrderedDict()  # name -> {"ram": n, "reason": s}
        self.unknown_kernel = OrderedDict()  # prefix -> {"ram": n}
        # unknown-bucket records kept for the anchor self-check scan
        # (new code families surface here first; their boot allocations
        # must be checked, not silently skipped)
        self.unknown_records = []

    def add_excluded(self, name, rec, reason, scope):
        e = self.excluded.setdefault(
            name, {"ram": 0, "reason": reason})
        classes = set(sec_classes(rec.osec, scope, rec.isec))
        if classes & set(RAM_SYMBOL_CLASSES):
            e["ram"] += rec.size


def exclusion_name_for_nonkernel(attr, scope):
    norm = attr.replace("\\", "/")
    m = re.search(r"([\w\-.+]+\.a)\(", norm)
    if m:
        return m.group(1)
    parts = [p for p in norm.split("/") if p]
    if parts and parts[0] == "obj":
        parts = parts[1:]
    if len(parts) >= 2:
        return "/".join(parts[:2])
    return norm or "<unknown>"


def run_funnel(recs, scope):
    res = FunnelResult()
    reasons = scope["exclude_reasons"]
    for r in recs:
        if not r.attr:
            continue
        is_kernel = any(p in r.attr for p in scope["kernel_lib_patterns"])
        # build/ holds out-of-tree prebuilt archives & intermediates
        # (e.g. liblos_cache.a rebuilt from SDK sources), not in-repo
        # kernel code -> treated as external
        if is_kernel and "/build/" in r.attr.replace("\\", "/"):
            is_kernel = False
        if not is_kernel:
            name = exclusion_name_for_nonkernel(r.attr, scope)
            res.add_excluded(name, r, reason_for_name(name), scope)
            continue
        prefix = attr_prefix(r.attr)
        if prefix in scope["group_prefixes"]:
            res.kernel.append(r)
            continue
        segs = kernel_tail_segments(r.attr)
        hit = None
        for s in segs[:-1]:
            if s in scope["excluded_path_segments"]:
                hit = s
                break
        if prefix in scope["excluded_prefixes"]:
            hit = prefix
        if hit is not None:
            name = "kernel/liteos_m:" + hit
            res.add_excluded(name, r, reasons.get(hit, ""), scope)
            continue
        # kernel library member outside the approved groups -> warn bucket
        bucket = res.unknown_kernel.setdefault(prefix, {"ram": 0})
        classes = set(sec_classes(r.osec, scope, r.isec))
        if classes & set(RAM_SYMBOL_CLASSES):
            bucket["ram"] += r.size
        res.unknown_records.append(r)
    return res


def exclude_sys_heap_pool(funnel, scope, macros):
    """D18: move the sys-heap pool carrier array `g_memStart` out
    of the kernel static attribution. The pool is shared infrastructure
    (kernel, components and product code all allocate from it); the
    kernel's real usage is what the dynamic anchors count.

    The array only exists on internal-heap boards (external-heap boards
    declare it extern, no storage); its size is cross-checked against
    LOSCFG_SYS_HEAP_SIZE when that macro is known. Returns
    {"size", "expected"} or None when no carrier record was found.
    """
    kept = []
    pool = None
    for r in funnel.kernel:
        if rec_name(r) == "g_memStart" or \
                r.isec in (".bss.g_memStart", ".data.g_memStart"):
            if pool is None:
                exp = macros.get("LOSCFG_SYS_HEAP_SIZE")
                pool = {"size": 0,
                        "expected": exp if isinstance(exp, int) else None}
            pool["size"] += r.size
            continue
        kept.append(r)
    if pool is None:
        return None
    funnel.kernel = kept
    funnel.excluded["g_memStart (sys 堆池数组)"] = {
        "ram": pool["size"],
        "reason": "堆池为共享基础设施, 不计入静态合计; "
                  "内核实际占用见动态申请",
    }
    return pool


SEC_LABELS = [
    ("ram_data", ".data"),
    ("ram_bss", ".bss"),
    ("ram_run", "RAM驻留代码段"),
]

# which classes make a symbol row appear in the RAM detail list
RAM_SYMBOL_CLASSES = ("ram_data", "ram_bss", "ram_run")

# sections kept in the reconciliation table (RAM-relevant only)
RAM_RECON_CLASSES = ("ram_data", "ram_bss", "ram_run", "stack", "heap")

SEC_DESC = {
    ".data": "已初始化数据 (计 RAM 一次)",
    ".bss": "未初始化数据 (资源池)",
    "RAM驻留代码段": ".text_sram/.text_tcm 等 (RAM 运行, 计 RAM)",
}


def static_totals(groups):
    """RAM static total per the RAM accounting rules."""
    ram = 0
    for _g, row in groups.items():
        ram += row["ram_data"] + row["ram_bss"] + row["ram_run"]
    return ram


def aggregate_kernel(recs, scope):
    """Per-group section totals + symbol rows."""
    groups = OrderedDict()
    symbols = []
    for r in recs:
        g = attr_prefix(r.attr)
        classes = sec_classes(r.osec, scope, r.isec)
        row = groups.setdefault(
            g, {**{k: 0 for k, _ in SEC_LABELS}, "total": 0})
        counted = False
        for cls in classes:
            if cls in row:
                row[cls] += r.size
                counted = True
        if counted:
            row["total"] += r.size
        symbols.append({
            "group": g, "member": attr_basename(r.attr),
            "symbol": rec_name(r), "isec": r.isec, "osec": r.osec or "",
            "class": "+".join(classes), "classes": classes, "size": r.size,
        })
    return groups, symbols


def outsec_total(outsec_sizes, scope, cls):
    total = 0
    for name, size in outsec_sizes.items():
        if cls in sec_classes(name, scope):
            total += size
    return total


def _recon_add_classes(seen, classes):
    for c in classes:
        if c not in seen:
            seen.append(c)


def _recon_attr_sums(recs, scope):
    """Per-osec attributed sums and the class sets seen on records."""
    attr_sum = OrderedDict()
    rec_cls = {}
    for r in recs:
        if r.osec:
            attr_sum[r.osec] = attr_sum.get(r.osec, 0) + r.size
            if r.size:
                _recon_add_classes(rec_cls.setdefault(r.osec, []),
                                   sec_classes(r.osec, scope, r.isec))
    return attr_sum, rec_cls


def _recon_note(classes, attr, fill, header):
    if "stack" in classes or "heap" in classes:
        return "链接器段 (无输入段归属)"
    if "ignore" in classes:
        return "非加载段"
    if attr == 0 and fill == 0 and header:
        return "无归属记录 (链接器生成)"
    return ""


def _recon_warn(classes, attr, diff):
    return (attr > 0 and abs(diff) > 64 and
            not ("stack" in classes or "heap" in classes or
                 "ignore" in classes))


def _recon_row(name, header, attr, fill, relax, classes):
    diff = header - attr - fill
    note = _recon_note(classes, attr, fill, header)
    warn = _recon_warn(classes, attr, diff)
    if warn and not note:
        note = "差额待查"
    return {
        "name": name, "classes": classes, "header": header,
        "attr": attr, "fill": fill, "relax": relax, "diff": diff,
        "note": note, "warn": warn,
    }


def outsec_reconciliation(recs, outsec_sizes, outsec_fills, outsec_relax,
                          scope):
    """Per-output-section reconciliation: section-header size vs the sum
    of attributed input-section records vs *fill* padding.

    Returns rows [{name, classes, header, attr, fill, relax, diff, note,
    warn}] where diff = header - attr - fill and `relax` reports bytes the
    attribution lost to string-merge/relaxation dedup. `warn` flags
    unexplained residuals (attribution gap suspicion) on loadable sections
    that have records.
    """
    attr_sum, rec_cls = _recon_attr_sums(recs, scope)
    rows = []
    for name, header in outsec_sizes.items():
        attr = attr_sum.get(name, 0)
        fill = outsec_fills.get(name, 0)
        relax = outsec_relax.get(name, 0)
        # container output sections (e.g. fbb .startup) classify per
        # record via the input section name -> show the actual class set
        classes = (sorted(set(rec_cls.get(name) or ())) if attr > 0
                   else []) or sec_classes(name, scope)
        # RAM-only tool: reconcile RAM-relevant sections only
        if not (set(classes) & set(RAM_RECON_CLASSES)):
            continue
        rows.append(_recon_row(name, header, attr, fill, relax, classes))
    return rows


Summary = namedtuple("Summary", (
    "ram_static", "stack", "heap",
    "persistent", "map_static", "dyn", "overhead",
    "ram_total", "ram_total_overhead"))


def build_summary(groups, items, outsec_sizes, scope):
    """Shared totals for the Excel and Markdown reports (D18)."""
    ram_static = static_totals(groups)
    stack = outsec_total(outsec_sizes, scope, "stack")
    heap = outsec_total(outsec_sizes, scope, "heap")
    persistent = [it for it in items
                  if it.status in ("dynamic", "source-static")
                  and not it.transient]
    map_static = [it for it in items
                  if it.status == "map-static" and not it.transient]
    dyn = sum(it.size for it in persistent if it.size)
    # node overhead accrues per HEAP NODE, not per item: merged anchors
    # (OsQueueInit allocates g_allQueue + g_staticQueue separately) and
    # multi-node explicit anchors (HPL's two ring buffers) each consume
    # their own header. source-static arrays consume no heap node.
    overhead = 0
    for it in persistent:
        if it.status != "dynamic" or it.size is None:
            continue
        for node_size in (it.sizes or [it.size]):
            ov = node_overhead(node_size, scope)
            if ov is not None:
                overhead += ov
    # D18: the heap pool is shared infrastructure and is NOT
    # part of the RAM total; it stays visible as an informational row.
    ram_total = ram_static + stack + dyn
    return Summary(ram_static, stack, heap, persistent,
                   map_static, dyn, overhead, ram_total,
                   ram_total + overhead)
