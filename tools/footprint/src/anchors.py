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
"""Dynamic-allocation anchors: anchor table, live scan, probe, evaluation."""

import os
import re
import shutil
import subprocess
import tempfile
from collections import OrderedDict
from pathlib import Path
from input import (REPO_ROOT, ExprEval, align_up, attr_basename,
                   die, preprocess_lines, read_file, tokenize_expr)

DEFAULT_ANCHORS = [
    {
        "item": "g_osTaskCBArray", "var": "g_osTaskCBArray",
        "file": "kernel/base/los_task.c", "func": "OsTaskInit",
        "when": "LOSCFG_BASE_CORE_TSK_LIMIT",
        "group": "base", "note": "任务控制块数组",
    },
    {
        "item": "g_allQueue", "var": "g_allQueue",
        "file": "kernel/base/los_queue.c", "func": "OsQueueInit",
        "when": "LOSCFG_BASE_IPC_QUEUE",
        "group": "ipc",
        "note": "队列控制块数组 (含静态队列池 g_staticQueue, "
                "QUEUE_STATIC_ALLOCATION 时同函数扫描计入)",
    },
    {
        "item": "g_osAllSem", "var": "g_osAllSem",
        "file": "kernel/base/los_sem.c", "func": "OsSemInit",
        "when": "LOSCFG_BASE_IPC_SEM",
        "group": "ipc", "note": "信号量控制块数组",
    },
    {
        "item": "g_osAllMux", "var": "g_osAllMux",
        "file": "kernel/base/los_mux.c", "func": "OsMuxInit",
        "when": "LOSCFG_BASE_IPC_MUX",
        "group": "ipc", "note": "互斥锁控制块数组",
    },
    {
        "item": "g_osAllRwsem", "var": "g_osAllRwsem",
        "file": "kernel/base/los_rwsem.c", "func": "OsRwsemInit",
        "when": "LOSCFG_BASE_IPC_RWSEM",
        "group": "ipc", "note": "读写锁控制块数组",
    },
    # debug companion arrays: allocated from the module's own Init hook, so
    # they exist only when BOTH the module and its debug switch are on.
    # The CB types are file-local typedefs, the compile probe cannot
    # measure their sizeof -> explicit layout-derived formulas (see note).
    {
        "item": "g_muxDebugArray", "var": "g_muxDebugArray",
        "file": "kernel/base/debug/los_mux_debug.c", "func": "OsMuxDbgInit",
        "expr": "LOSCFG_BASE_IPC_MUX_LIMIT * 16",
        "when": "LOSCFG_DEBUG_MUTEX && LOSCFG_BASE_IPC_MUX",
        "group": "ipc",
        "note": "互斥锁调试数组 {指针,UINT64} 对齐 16B (类型为文件局部)",
    },
    {
        "item": "g_queueDebugArray", "var": "g_queueDebugArray",
        "file": "kernel/base/debug/los_queue_debug.c", "func": "OsQueueDbgInit",
        "expr": "OS_ALL_IPC_QUEUE_LIMIT * 16",
        "when": "LOSCFG_DEBUG_QUEUE && LOSCFG_BASE_IPC_QUEUE",
        "group": "ipc",
        "note": "队列调试数组 {指针,UINT64} 对齐 16B (类型为文件局部)",
    },
    {
        "item": "g_semDebugArray", "var": "g_semDebugArray",
        "file": "kernel/base/debug/los_sem_debug.c", "func": "OsSemDbgInit",
        "expr": "LOSCFG_BASE_IPC_SEM_LIMIT * 24",
        "when": "LOSCFG_DEBUG_SEMAPHORE && LOSCFG_BASE_IPC_SEM",
        "group": "ipc",
        "note": "信号量调试数组 {UINT16,UINT64,指针} 对齐 24B (类型为文件局部)",
    },
    {
        "item": "g_osSwtmrCBArray", "var": "g_osSwtmrCBArray",
        "file": "kernel/base/los_swtmr.c", "func": "OsSwtmrInit",
        "when": "LOSCFG_BASE_CORE_SWTMR",
        "group": "base", "note": "软件定时器控制块数组",
    },
    {
        "item": "g_cpup", "var": "g_cpup",
        "file": "kernel/extended/cpup/los_cpup.c", "func": "OsCpupInit",
        "when": "LOSCFG_KERNEL_CPUP",
        "group": "cpup", "note": "CPU 占用率统计数组",
    },
    # v1.4 full-inventory additions (R13): init-permanent heap allocations
    # whose sizes are fully config-determined, but whose allocation sites
    # cannot be live-scanned (parameter assignment / task-creation internal).
    {
        "item": "g_osTraceBuf", "var": "g_osTraceBuf",
        "file": "kernel/extended/trace/los_trace.c",
        "func": "OsTraceInitEntry",
        "expr": "LOSCFG_TRACE_BUFFER_SIZE",
        "when": "LOSCFG_KERNEL_TRACE && LOSCFG_KERNEL_MEM_ALLOC",
        "group": "trace",
        "note": "trace 离线缓冲 (OsTraceBufInit 内 buf 为调用方传参, "
                "无全局变量可扫描; 平台初始化传 NULL 即走堆分配)",
    },
    {
        "item": "g_bhTaskId", "var": "g_bhTaskId",
        "file": "kernel/base/los_hwi.c", "func": "OsHwiBottomHalfInit",
        "expr": "LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE",
        "when": "LOSCFG_HWI_BOTTOM_HALF",
        "group": "base",
        "note": "中断下半部任务栈 (LOS_TaskCreate 恒动态, 无静态分支; "
                "栈无全局指针, 以 4B 任务 ID 符号作 map 存在标记)",
    },
    # v1.6 审计补遗 (2026-10-27 QEMU 堆节点直方图对拍): trace 延迟打包
    # 任务栈与 HPL 开机常驻分配此前均未入表。
    {
        "item": "g_deferPackerTaskId", "var": "g_deferPackerTaskId",
        "file": "kernel/extended/trace/los_trace.c",
        "func": "OsCreateDeferPackerTask",
        "expr": "LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE",
        "when": "LOSCFG_KERNEL_TRACE",
        "group": "trace",
        "note": "trace 延迟打包任务栈 (OsCreateDeferPackerTask 在 trace 初始化"
                "内无条件创建; 栈无全局指针, 以 4B 任务 ID 符号作 map 存在标记)",
    },
    # HPL (High Performance Log, kernel/extended/hpl): OsHplInit 经
    # LOS_SYS_INIT(SYS_INIT_LEVEL_KERNEL_ADDITION) 开机自动执行, 宏开启即
    # 分配, 无需产品调用。尺寸常量为模块内部宏/类型 (los_hpl_logger_pri.h/
    # los_hpl.h), 锚点以 probe_headers 声明私有头, 由编译探针引入实测;
    # lhs 限定本锚点只取目标分配语句 (OsHplInit 内 3 处分配分属 3 个锚点,
    # 避免合并误加)。数值经 QEMU 运行时堆节点直方图逐块对拍 (an386
    # task_lock_free: 1360/2064*3/2580/1040*2 全命中)。
    {
        "item": "g_hplCBArray", "var": "g_hplCBArray",
        "file": "kernel/extended/hpl/src/task_lock_free/los_hpl_logger.c",
        "func": "OsHplInit",
        "lhs": "g_hplCBArray",
        "probe_headers": ["kernel/extended/hpl/include/los_hpl_logger_pri.h"],
        "when": "LOSCFG_KERNEL_HPL && LOSCFG_KERNEL_HPL_TASK_LOCK_FREE",
        "group": "hpl",
        "note": "HPL 任务句柄数组 (HPL_CB_ARRAY_MAX × sizeof(HplHandle); "
                "数组上限宏与句柄类型均以探针引入私有头实测)",
    },
    {
        "item": "g_runtimeLogger.compressBuf",
        "file": "kernel/extended/hpl/src/task_lock_free/los_hpl_logger.c",
        "func": "OsHplInit",
        "lhs": "g_runtimeLogger.compressBuf",
        "probe_headers": ["kernel/extended/hpl/include/los_hpl_logger_pri.h"],
        "when": "LOSCFG_KERNEL_HPL && LOSCFG_KERNEL_HPL_TASK_LOCK_FREE",
        "evidence": "g_runtimeLogger",
        "group": "hpl",
        "note": "HPL 压缩参数缓冲 (HPL_COMPRESS_ARGS_BUF_SIZE, "
                "los_hpl_logger_pri.h)",
    },
    {
        "item": "g_runtimeLogger.fmtRingbuf",
        "file": "kernel/extended/hpl/src/los_hpl_c_preprocessor.c",
        "func": "OsHplCPreprocessInit",
        "lhs": "fmtBuf",
        "probe_headers": ["kernel/extended/hpl/include/los_hpl_logger_pri.h"],
        "when": "LOSCFG_KERNEL_HPL && LOSCFG_KERNEL_HPL_TASK_LOCK_FREE",
        "when_not": "LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR",
        "evidence": "g_runtimeLogger",
        "group": "hpl",
        "note": "HPL 格式串环形缓冲 (HPL_LOGGER_FMT_BUF_SIZE; "
                "SCRIPT_PREPROCESSOR 开启时 OsHplCPreprocessInit 不执行)",
    },
    {
        "item": "g_runtimeLogger.abandonedLog",
        "file": "kernel/extended/hpl/src/task_lock_free/los_hpl_logger.c",
        "func": "OsHplInit",
        "lhs": "abandonedBuf",
        "probe_headers": ["kernel/extended/hpl/include/los_hpl_logger_pri.h"],
        "when": "LOSCFG_KERNEL_HPL && LOSCFG_KERNEL_HPL_TASK_LOCK_FREE",
        "evidence": "g_runtimeLogger",
        "group": "hpl",
        "note": "HPL 弃置日志缓冲 (HPL_ABANDONED_LOG_BUF_SIZE, "
                "los_hpl_logger_pri.h)",
    },
    {
        "item": "g_runtimeLogger.compressionTaskId",
        "file": "kernel/extended/hpl/src/task_lock_free/los_hpl_logger.c",
        "func": "OsHplCreateCompressTask",
        "expr": "HPL_COMPRESS_THREAD_STACK_SIZE",
        "probe_headers": ["kernel/extended/hpl/include/los_hpl_logger_pri.h"],
        "when": "LOSCFG_KERNEL_HPL && LOSCFG_KERNEL_HPL_TASK_LOCK_FREE",
        "evidence": "g_runtimeLogger",
        "group": "hpl",
        "note": "HPL 压缩任务栈 (HPL_COMPRESS_THREAD_STACK_SIZE, 非 LP64 分支; "
                "LP64 分支为 4096, LiteOS-M 现网无 LP64 目标)",
    },
    {
        "item": "g_interruptHpl/g_exceptionHpl",
        "var": "g_interruptHpl",
        "file": "kernel/extended/hpl/src/task_lock_free/los_hpl_logger.c",
        "func": "OsHplCreateExcIntHandle",
        "expr": ["LOS_HPL_RINGBUF_DEFAULT_SIZE", "LOS_HPL_RINGBUF_DEFAULT_SIZE"],
        "probe_headers": ["kernel/include/los_hpl.h"],
        "when": "LOSCFG_KERNEL_HPL && LOSCFG_KERNEL_HPL_TASK_LOCK_FREE",
        "group": "hpl",
        "note": "中断/异常 HPL 暂存环形缓冲 2 处 "
                "(LOS_HPL_RINGBUF_DEFAULT_SIZE, los_hpl.h)",
    },
    {
        "item": "cpuLessOneSec", "var": "cpuLessOneSec",
        "file": "kernel/base/los_task.c", "func": "GetAllTskCpupInfo",
        "when": "LOSCFG_BASE_CORE_CPUP",
        "group": "base", "transient": True,
        "note": "代码定义: los_task.c GetAllTskCpupInfo 出参 "
                "CPUP_INFO_S **cpuLessOneSec (函数参数, 非全局符号; "
                "同型出参 cpuLessOneSec/cpuTenSec/cpuOneSec 共 3 个, "
                "公式已合并 x3); 调用期申请、用后即释, 瞬态",
    },
    # variable-length retry buffer: doubles until the output fits, freed
    # right after use -> transient lower bound only (SIZEBUF is a
    # file-local macro, value 256)
    {
        "item": "bBuf", "var": "bBuf",
        "file": "kernel/base/los_printf.c", "func": "OsVprintf",
        "expr": "512",
        "when": "LOSCFG_KERNEL_PRINTF_SIZE_EXTEND",
        "group": "base", "transient": True,
        "note": "代码定义: los_printf.c OsVprintf 内局部变量 CHAR *bBuf "
                "(非全局符号; 栈缓冲不足时转堆, SIZEBUF 256 起步倍增, "
                "用后即释); 登记值为首次堆申请下界 512",
    },
    # task stacks: form migrates between versions (static array vs dynamic)
    {
        "item": "g_osIdleTaskStack", "var": "g_osIdleTaskStack",
        "file": "kernel/base/los_task.c", "func": "OsIdleTaskCreate",
        "kind": "stack", "dyn_macro": "KERNEL_TSK_IDLE_STACK_SIZE",
        "static_when": "LOSCFG_TASK_STACK_STATIC_ALLOCATION",
        "evidence": "OsIdleTaskCreate",
        "group": "base", "note": "Idle 任务栈",
    },
    {
        "item": "g_swtmrTaskStack", "var": "g_swtmrTaskStack",
        "file": "kernel/base/los_swtmr.c", "func": "OsSwtmrTaskCreate",
        "kind": "stack", "dyn_macro": "LOSCFG_BASE_CORE_TSK_SWTMR_STACK_SIZE",
        "static_when": "LOSCFG_TASK_STACK_STATIC_ALLOCATION",
        "when": "LOSCFG_BASE_CORE_SWTMR",
        "when_not": "LOSCFG_BASE_CORE_SWTMR_IN_ISR",
        "evidence": "g_osSwtmrCBArray",
        "group": "base", "note": "软件定时器任务栈",
    },
]

# simple builtin type sizes for static array declarations
BUILTIN_SIZES = {
    "VOID": 1, "void": 1, "char": 1, "CHAR": 1, "UINT8": 1, "INT8": 1,
    "UINT16": 2, "INT16": 2, "SHORT": 2,
    "UINT32": 4, "INT32": 4, "int": 4, "INT": 4, "BOOL": 4, "FLOAT": 4,
    "UINT64": 8, "INT64": 8, "DOUBLE": 8,
}


class DynamicItem:

    def __init__(self, item):
        self.item = item.get("item", "?")
        self.group = item.get("group", "")
        self.note = item.get("note", "")
        self.status = ""      # map-static / dynamic / disabled / unknown
        self.size = None      # bytes (persistent items only)
        self.sizes = []       # per-allocation-node sizes (overhead basis)
        self.expr = ""        # formula found in source
        self.source = ""      # 数据来源标注
        self.detail = ""
        self.transient = bool(item.get("transient", False))
        self.warn = ""


def find_static_in_map(recs, var):
    total = 0
    for r in recs:
        if r.attr and r.isec in (".bss." + var, ".data." + var,
                                 ".rodata." + var, ".text." + var):
            total += r.size
    return total or None


def eval_with(expr, macros, probe_values, sizeof_table):
    """Evaluate a dynamic formula.

    Values come from the compile probe (probe_values) or, exclusively,
    from a user-given sizeof table (sizeof_table) -- the two modes never
    mix (see process_anchors).
    """

    def vres(name):
        if "mac:" + name in probe_values:
            return probe_values["mac:" + name]
        if "mac:" + name in sizeof_table:
            return sizeof_table["mac:" + name]
        if name in macros and isinstance(macros[name], int):
            return macros[name]
        raise ValueError("unresolved identifier: %s" % name)

    def sres(t):
        key = "sz:" + t
        if key in sizeof_table:
            return sizeof_table[key]
        if key in probe_values:
            return probe_values[key]
        raise ValueError("unresolved sizeof(%s)" % t)

    return ExprEval(vres, sres).eval(expr)


def eval_gate(gate, macros):
    if not gate:
        return True
    try:
        return bool(eval_with(gate, macros, {}, {}))
    except ValueError:
        return False


def _gate_reason(a, macros):
    """Config gates: return the disabled reason, or None if enabled."""
    if not eval_gate(a.get("when"), macros):
        return "配置未启用 (%s)" % (a.get("when") or "")
    if a.get("when_not"):
        names = ([a["when_not"]] if isinstance(a["when_not"], str)
                 else a["when_not"])
        blocked = [m for m in names if macros.get(m) not in (None, 0)]
        if blocked:
            return "不适用 (%s 已定义)" % ",".join(blocked)
    return None


def _scan_stack_anchor(a, macros, src_root, it, warnings, queue_eval):
    """Stack anchors: source-static array, else dynamic task-stack macro."""
    # static array in source (only meaningful when gate on)
    if eval_gate(a.get("static_when"), macros):
        dims, elem = scan_static_array(
            src_root, a["file"], a["var"], macros)
        if dims is not None:
            it.status = "source-static"
            dim_expr = "*".join("(%s)" % d if d.strip() else "1"
                                for d in dims)
            if elem in BUILTIN_SIZES:
                full = "(%s) * %d" % (dim_expr, BUILTIN_SIZES[elem])
            else:
                full = "(%s) * sizeof(%s)" % (dim_expr, elem)
            it.expr = full
            it.source = "源码静态数组 (map 中缺失, 疑似版本不一致)"
            warnings.append(
                "锚点 %s: 源码为静态数组但 map 无此符号, "
                "map 与源码版本可能不一致" % a["item"])
            queue_eval(it, full)
            return
    # not static in source -> dynamic stack via task creation
    dyn = a.get("dyn_macro", "")
    if dyn:
        it.status = "dynamic"
        it.expr = dyn
        it.source = "源码推算 (%s: %s, 任务栈动态申请)"\
            % (a["file"], a.get("func", "LOS_TaskCreate"))
        queue_eval(it, dyn)
    else:
        it.status = "unknown"
        it.source = "未找到分配公式"
        warnings.append("锚点 %s: 未找到栈定义或申请公式" % a["item"])


def _build_cb_expr(src_root, a, res, macros):
    """Merge scanned allocations into one formula (alias-resolved).

    Returns (display_expr, node_count, per_node_exprs) where
    per_node_exprs repeats each allocation site's resolved formula
    once per node -- the per-node sizes are the node-overhead basis
    (e.g. OsQueueInit allocates g_allQueue and g_staticQueue as two
    separate heap nodes).
    """
    total_expr = []
    per_node = []
    count = 0
    for expr, cnt in res:
        # resolve runtime variable aliases (e.g. g_taskMaxNum)
        e = expr
        for ident in set(re.findall(r"\b[A-Za-z_]\w*\b", e)):
            if ident.startswith("LOSCFG_") or ident == "sizeof":
                continue
            alias = resolve_var_alias(src_root, a["file"], ident, macros)
            if alias and alias != ident:
                e = re.sub(r"\b%s\b" % re.escape(ident),
                           "(%s)" % alias, e)
        if cnt > 1:
            total_expr.append("(%s) * %d" % (e, cnt))
        else:
            total_expr.append(e)
        per_node.extend([e] * cnt)
        count += cnt
    if len(total_expr) == 1:
        return total_expr[0], count, per_node
    return " + ".join(total_expr), count, per_node


def _probe_needs_of_expr(expr, macros, sizeof_table, need_sizeof, need_macros):
    for tok, val in tokenize_expr(expr):
        if tok == "sizeof":
            if ("sz:" + val) not in sizeof_table:
                need_sizeof.add(val)
            continue
        if tok == "ident" and val not in macros \
                and ("mac:" + val) not in sizeof_table:
            need_macros.add(val)


def _collect_probe_needs(pending_exprs, macros, sizeof_table):
    """Macros and sizeof types required by the queued expressions."""
    need_sizeof = set()
    need_macros = set()
    for it, expr in pending_exprs:
        _probe_needs_of_expr(expr, macros, sizeof_table,
                             need_sizeof, need_macros)
    return need_sizeof, need_macros


def _probe_batch_values(cc, macros, need_sizeof, need_macros,
                        board_dirs, warnings, extra_headers):
    """Compile-probe inside a temp dir; resolved values ({} on failure)."""
    with tempfile.TemporaryDirectory(prefix="footprint_probe_") as td:
        vals, err = run_probe(cc, macros, need_sizeof, need_macros,
                               Path(td), board_dirs, extra_headers)
    if vals:
        if err:
            warnings.append("probe 部分失败: %s" % err.splitlines()[-1])
        return vals
    if err:
        warnings.append("probe 编译失败: %s" % err.splitlines()[-1])
    return {}


def _run_probe_batch(cc, macros, need_sizeof, need_macros, board_dirs,
                     warnings, extra_headers=()):
    """Compile-probe the missing sizeofs/macros; return resolved values."""
    if not cc:
        die("toolchain not found: dynamic values need the compile probe "
            "(%d sizeof + %d macros) but no cross compiler was resolved; "
            "run on a machine with a build environment, or pass --cc "
            "explicitly" % (len(need_sizeof), len(need_macros)))
    return _probe_batch_values(cc, macros, need_sizeof, need_macros,
                               board_dirs, warnings, extra_headers)


def _anchor_gated_off(a, macros, it, items):
    """Phase 0: config gate -- disabled anchors never enter the pipeline."""
    reason = _gate_reason(a, macros)
    if reason is None:
        return False
    it.status = "disabled"
    it.source = reason
    items.append(it)
    return True


def _anchor_map_static(a, recs, it, items):
    """Phase 1: already static in this build's map?

    A <=8B hit is the declaration POINTER of a dynamic array
    (e.g. `LosTaskCB *g_osTaskCBArray`), not the array itself.
    """
    static_size = find_static_in_map(recs, a.get("var", ""))
    if static_size is None or static_size <= 8:
        return False
    it.status = "map-static"
    it.size = static_size
    it.source = "map 真值 (静态符号, 已计入静态统计)"
    it.detail = "%s 在 map 中为静态符号" % a.get("var")
    items.append(it)
    return True


def _anchor_no_evidence(a, recs, it, items, warnings):
    """Phase 1a: binary-presence evidence check (D26).

    A config-enabled anchor whose module left no companion symbol in
    this build's map must not report a size -- the config may name a
    feature that BUILD.gn actually excluded (config-on / binary-off
    mismatch). Evidence defaults to the anchor's own var (declaration
    pointer or static array, always emitted when the module compiles,
    -fdata-sections); member anchors name their container struct; stack
    anchors name a module marker. Transient items have no symbol by
    design; with no map records at all (degraded estimation) the check
    is skipped.
    """
    if not recs or a.get("transient"):
        return False
    ev = a.get("evidence")
    if not ev and a.get("var"):
        ev = a["var"]
    ev = [ev] if isinstance(ev, str) else ev
    if ev and all(find_static_in_map(recs, n) is None for n in ev):
        it.status = "disabled"
        it.source = "map 无伴生符号 %s (配置启用但未编入二进制?)" % "/".join(ev)
        warnings.append(
            "锚点 %s: 配置已启用但 map 无伴生符号 %s, "
            "疑似未编入本构建, 已按 0 计" % (a["item"], "/".join(ev)))
        items.append(it)
        return True
    return False


def _anchor_expr(a, it, items, queue_eval):
    """Phase 1b: explicit formula override (file-local types /
    variable-length allocations that the live scan cannot express).
    `expr` may be a list -- one entry per heap node (e.g. HPL's two ring
    buffers).
    """
    if not a.get("expr"):
        return False
    it.status = "dynamic"
    exprs = a["expr"] if isinstance(a["expr"], list) else [a["expr"]]
    it.expr = " + ".join(exprs)
    it.source = "登记公式 (%s: %s)" % (a["file"], a.get("func", "-"))
    for e in exprs:
        queue_eval(it, e)
    items.append(it)
    return True


def _anchor_scan_formula(a, recs, macros, src_root, it, items,
                         warnings, queue_eval):
    """Phase 2 (kind == 'cb'): scan the live source formula."""
    res, err = scan_allocations(src_root, a["file"], a["func"], macros,
                                lhs=a.get("lhs"))
    if err:
        it.status = "unknown"
        it.source = "锚点失配"
        it.warn = err
        warnings.append("锚点 %s: %s" % (a["item"], err))
    else:
        it.status = "dynamic"
        it.source = "源码推算 (%s: %s)" % (a["file"], a.get("func", "-"))
        it.expr, count, node_exprs = _build_cb_expr(src_root, a, res, macros)
        it.detail = "%d 处分配" % count
        for e in node_exprs:
            queue_eval(it, e)
    items.append(it)


def _probe_extra_headers(anchors, macros):
    """Per-anchor probe headers (repo-relative), config-active only.

    Declared by anchors whose size operands live in module-private
    headers (e.g. HPL); collected upfront for every config-active anchor
    so both scan and expr paths can resolve their operands through the
    compile probe.
    """
    extra_headers = []
    seen_hdr = set()
    for a in anchors:
        if _gate_reason(a, macros) is not None:
            continue
        for h in a.get("probe_headers", []):
            if h not in seen_hdr:
                seen_hdr.add(h)
                extra_headers.append(h)
    return extra_headers


def _eval_pending(pending_exprs, macros, probe_values, sizeof_table,
                  warnings):
    failed = set()
    for it, expr in pending_exprs:
        try:
            v = eval_with(expr, macros, probe_values, sizeof_table)
            it.sizes.append(v)
            it.size = (it.size or 0) + v
        except ValueError as e:
            failed.add(id(it))
            it.warn = str(e)
            warnings.append("动态项 %s: 求值失败 %s (公式: %s)"
                            % (it.item, e, expr))
    return failed


def _mark_failed_items(items, failed):
    """An item evaluates all-or-nothing: one failed node invalidates the
    whole merged size (partial sums must never reach the totals)."""
    for it in items:
        if id(it) in failed:
            it.status = "unknown"
            it.source = "公式求值失败"
            it.size = None
            it.sizes = []


def process_anchors(anchors, recs, macros, scope, src_root, cc,
                    sizeof_table, warnings, board_dirs=None):
    items = []
    pending_exprs = []   # (item_obj, expr)

    def queue_eval(it, expr):
        pending_exprs.append((it, expr))

    extra_headers = _probe_extra_headers(anchors, macros)
    for a in anchors:
        it = DynamicItem(a)
        if _anchor_gated_off(a, macros, it, items):
            continue
        if _anchor_map_static(a, recs, it, items):
            continue
        if _anchor_no_evidence(a, recs, it, items, warnings):
            continue
        if _anchor_expr(a, it, items, queue_eval):
            continue
        if a.get("kind", "cb") == "stack":
            _scan_stack_anchor(a, macros, src_root, it, warnings, queue_eval)
            items.append(it)
            continue
        _anchor_scan_formula(a, recs, macros, src_root, it, items,
                             warnings, queue_eval)

    # 3) batch-evaluate queued expressions.
    # sizeof-table mode is exclusive: a given table fully replaces the
    # probe (no merging, no partial coverage) -- entries it lacks stay
    # unresolved and are annotated in the report.
    probe_values = {}
    if not sizeof_table:
        need_sizeof, need_macros = _collect_probe_needs(
            pending_exprs, macros, sizeof_table)
        if need_sizeof or need_macros:
            probe_values = _run_probe_batch(
                cc, macros, need_sizeof, need_macros,
                board_dirs or [], warnings, extra_headers)
    failed = _eval_pending(pending_exprs, macros, probe_values,
                           sizeof_table, warnings)
    _mark_failed_items(items, failed)
    return items, probe_values


def node_overhead(size, scope):
    hdr = scope.get("node_header_bytes", 8)
    align = scope.get("node_align", 8)
    if size is None:
        return None
    return align_up(size, align) - size + hdr


def expand_macros(expr, defines, max_depth=32):
    """Textually substitute known object-like macros into expr."""
    for _ in range(max_depth):
        changed = False

        def sub(m):
            nonlocal changed
            name = m.group(0)
            if name in defines:
                changed = True
                return "(" + defines[name] + ")"
            return name
        new = re.sub(r"\b[A-Za-z_]\w*\b", sub, expr)
        expr = new
        if not changed:
            break
    return expr


def function_body_lines(lines, func):
    """Return (start, end) line indices of function `func` body, or None."""
    for i, l in enumerate(lines):
        if not re.search(r"\b%s\s*\(" % re.escape(func), l):
            continue
        # the opening brace must appear within 10 lines of the match
        # (a call site inside another function usually has none)
        depth = 0
        opened = False
        for k in range(i, len(lines)):
            depth += lines[k].count("{") - lines[k].count("}")
            if "{" in lines[k]:
                opened = True
            if opened and depth <= 0:
                return (i, k)
            if not opened and k >= i + 9:
                break
    return None


def _paren_chars(text, s, depth, out):
    """Consume text[s:] into `out`; (True, depth) once the closing
    paren of the expression opened at depth 0 has been consumed."""
    while s < len(text):
        c = text[s]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "(" and depth == 1:
            s += 1
            continue
        if c == ")" and depth == 0:
            return True, depth
        if depth >= 1:
            out.append(c)
        s += 1
    return False, depth


def extract_parenthesized(lines, li, col):
    """Extract the full parenthesized text starting at (li, col=='(')."""
    depth = 0
    out = []
    i = li
    while i < len(lines):
        text = lines[i]
        s = col if i == li else 0
        done, depth = _paren_chars(text, s, depth, out)
        if done:
            return "".join(out)
        out.append(" ")
        i += 1
        if i - li > 12:
            break
    return None


def split_top_args(s):
    args = []
    depth = 0
    cur = []
    for c in s:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "," and depth == 0:
            args.append("".join(cur))
            cur = []
        else:
            cur.append(c)
    args.append("".join(cur))
    return args


def strip_casts(expr):
    expr = re.sub(r"\(\s*[A-Za-z_][\w\s\*]*\*\s*\)\s*", " ", expr)
    # width casts (e.g. (UINT32)sizeof(HplHandle)): shield sizeof's own
    # parens with a sentinel first, so stripping cannot eat into them
    expr = re.sub(r"\bsizeof\s*\(", "sizeof\x00(", expr)
    expr = re.sub(r"\(\s*(?:UINT|INT)(?:8|16|32|64)?\s*\)\s*", " ", expr)
    expr = expr.replace("sizeof\x00(", "sizeof(")
    return re.sub(r"\s+", " ", expr).strip()


def find_last_assignment(lines, upto, name):
    pat = re.compile(r"\b%s\s*=\s*([^;]+);" % re.escape(name))
    for i in range(upto - 1, max(-1, upto - 60), -1):
        m = pat.search(lines[i])
        if m:
            return m.group(1).strip()
    return None


def _statement_lvalue(blines, i, col):
    """Assignment target of the alloc statement at line i, byte col.

    Looks at the text before the LOS_MemAlloc call: an `=` on the same
    line (take the identifier/member-path right before it) or a line
    ending with `=` just above (continuation). Returns None when the
    call result is not directly assigned (e.g. a nested argument).
    """
    txt = blines[i][:col]
    if "=" in txt:
        pre = txt.rsplit("=", 1)[0]
        m = re.search(r"([A-Za-z_][\w.]*)\s*$", pre)
        return m.group(1) if m else None
    for j in range(i - 1, max(-1, i - 3), -1):
        prev = blines[j].rstrip()
        if prev.endswith("="):
            m = re.search(r"([A-Za-z_][\w.]*)\s*$", prev[:-1].rstrip())
            if m:
                return m.group(1)
    return None


def _scan_alloc_match(blines, i, m, lhs, defines, results):
    """Record one LOS_MemAlloc site into `results` (expr -> count)."""
    if lhs is not None:
        lv = _statement_lvalue(blines, i, m.start())
        if lv is None or lv != lhs:
            return
    inner = extract_parenthesized(blines, i, m.end() - 1)
    if inner is None:
        return
    args = split_top_args(inner)
    if len(args) < 2:
        return
    expr = strip_casts(args[1])
    if re.fullmatch(r"\w+", expr or ""):
        resolved = find_last_assignment(blines, i, expr)
        if resolved:
            expr = strip_casts(resolved)
    if expr:
        if defines:
            expr = expand_macros(expr, defines)
        results[expr] = results.get(expr, 0) + 1


def scan_allocations(src_root, rel_file, func, macros, lhs=None):
    """Find LOS_MemAlloc sites in `func` and return [(expr, count), ...].

    File-local #define chains (e.g. OS_CPUP_CB_MAX_NUM in los_cpup.c) are
    expanded textually so the returned expressions only reference .config
    macros, header macros and sizeof(TYPE). With `lhs` set, only the
    allocation assigned to that target (plain/global/member path) counts
    -- for anchor functions holding several distinct allocations.
    """
    text = read_file(src_root / rel_file)
    if text is None:
        return None, "file not found: %s" % rel_file
    lines = text.splitlines()
    active, defines = preprocess_lines(lines, macros)
    body = function_body_lines(active, func)
    if body is None:
        return None, "function %s not found in %s" % (func, rel_file)
    b0, b1 = body
    blines = active[b0:b1 + 1]
    results = OrderedDict()
    for i, l in enumerate(blines):
        for m in re.finditer(r"LOS_MemAlloc(?:Align)?\s*\(", l):
            _scan_alloc_match(blines, i, m, lhs, defines, results)
    if not results:
        if lhs is not None:
            return None, ("no LOS_MemAlloc assigned to lhs=%s found in %s()"
                          % (lhs, func))
        return None, "no LOS_MemAlloc found in %s()" % func
    return list(results.items()), None


def scan_static_array(src_root, rel_file, var, macros):
    """Find a static array definition `TYPE var[DIM...];` -> (dims, elem).

    Tolerates alignment/attribute macros between the array declarator and
    the terminating semicolon (e.g. `UINT8 g_x[N][M] LITE_OS_ATTR_ALIGN(..);`)
    """
    text = read_file(src_root / rel_file)
    if text is None:
        return None, "file not found: %s" % rel_file
    lines = text.splitlines()
    active, _ = preprocess_lines(lines, macros)
    joined = "\n".join(active)
    m = re.search(
        r"([A-Za-z_][\w]*(?:\s+[A-Za-z_][\w]*)*?)\s+%s\s*((?:\[[^\]\[]*\]\s*)+[^;]*);"
        % re.escape(var), joined)
    if not m:
        return None, None
    types = m.group(1).split()
    elem = types[-1] if types else "int"
    dims = re.findall(r"\[([^\]]*)\]", m.group(2))
    return dims, elem


def resolve_var_alias(src_root, rel_file, var, macros):
    """Resolve a runtime variable like g_taskMaxNum = MACRO; -> MACRO."""
    text = read_file(src_root / rel_file)
    if text is None:
        return None
    active, _ = preprocess_lines(text.splitlines(), macros)
    m = re.search(r"\b%s\s*=\s*([A-Za-z_][\w.]*)\s*;" % re.escape(var),
                  "\n".join(active))
    return m.group(1) if m else None


FUNC_HDR_RE = re.compile(r"^[A-Za-z_][^;=]*?\b([A-Za-z_]\w*)\s*\([^()]*\)\s*$")
G_VAR_DECL_RE = re.compile(r"\b(g_\w+)\s*(?:\[[^\]]*\])?\s*(?:=|;)")
# lvalue may be a member path (e.g. g_runtimeLogger.compressBuf): its
# root global must be file-scope (G_VAR_DECL_RE) to count
G_ALLOC_RE = re.compile(r"\b(g_\w+(?:\.\w+)*)\s*=\s*[^;]*LOS_MemAlloc")


def _uncovered_srcnames(funnel):
    """Source basenames of this build's kernel-scope object members."""
    srcnames = set()
    for r in list(funnel.kernel) + getattr(funnel, "unknown_records", []):
        member = attr_basename(r.attr)
        if not member.endswith(".o"):
            continue
        srcnames.add(member[:-2].split(".")[-1] + ".c")
    return srcnames


def _uncovered_rels(srcnames, src_root):
    """Kernel .c files matching `srcnames` (testsuites excluded)."""
    rels = OrderedDict()
    for name in sorted(srcnames):
        for p in (src_root / "kernel").rglob(name):
            if "testsuites" in p.parts or p.suffix != ".c":
                continue
            rels[str(p.relative_to(src_root)).replace("\\", "/")] = True
    return rels


def _scan_alloc_line(l, cur_func, depth, filevars, found):
    """One active source line: file-scope g_* decls (depth 0) and
    `g_* = ...LOS_MemAlloc` assignments (inside functions)."""
    if depth == 0:
        m = FUNC_HDR_RE.match(l)
        if m:
            cur_func = m.group(1)
        for dm in G_VAR_DECL_RE.finditer(l):
            filevars.add(dm.group(1))
    else:
        for am in G_ALLOC_RE.finditer(l):
            var = am.group(1)
            root = var.split(".")[0]
            if root in filevars and var not in found:
                found[var] = cur_func
    return cur_func, depth + l.count("{") - l.count("}")


def _scan_file_allocs(active):
    """File-scope g_* vars and active g_* = ...LOS_MemAlloc sites."""
    filevars = set()
    found = OrderedDict()   # var -> enclosing function
    cur_func = ""
    depth = 0
    for l in active:
        cur_func, depth = _scan_alloc_line(l, cur_func, depth, filevars, found)
    return filevars, found


def _alloc_covered(var, func, rel, anchors):
    """A member path names one exact object, so it is covered only by an
    anchor registering that exact item/var; plain globals are covered by
    same file + same function, or same file + same variable."""
    if "." in var:
        return any(a.get("file") == rel and
                   (a.get("item") == var or a.get("var") == var)
                   for a in anchors)
    return any(a.get("file") == rel and
               (a.get("func") == func or a.get("var") == var)
               for a in anchors)


def _scan_uncovered_file(rel, anchors, macros, src_root, uncovered):
    text = read_file(src_root / rel)
    if text is None:
        return
    active, _ = preprocess_lines(text.splitlines(), macros)
    _filevars, found = _scan_file_allocs(active)
    for var, func in found.items():
        if not _alloc_covered(var, func, rel, anchors):
            uncovered.append((rel, var, func))


def scan_uncovered_allocs(funnel, anchors, macros, src_root):
    """Anchor-coverage self-check (never-silent principle for dynamics).

    Scans the source files whose object members are in this build's kernel
    scope -- approved groups AND the unknown-prefix warning bucket (new
    upstream code families land there first, and their boot allocations
    must surface instead of silently dropping out of both the static
    groups and this check, as happened with HPL) -- for ACTIVE
    file-scope `g_* = ...LOS_MemAlloc(...)` assignments (member-path
    targets like `g_runtimeLogger.compressBuf` included: the member's
    root global must be file-scope), and reports the ones no anchor
    covers. #if-gated-off code contributes nothing.
    Returns [(rel_file, var, func), ...].
    """
    rels = _uncovered_rels(_uncovered_srcnames(funnel), src_root)
    uncovered = []
    for rel in rels:
        _scan_uncovered_file(rel, anchors, macros, src_root, uncovered)
    return uncovered


PROBE_INCLUDES = [
    "kernel/include",
    "kernel/base/include",
    "kernel/base/mem/tlsf",
    "kernel/extended/cpup",
    "kernel/extended/backtrace",
    "kernel/extended/trace",
    "kernel/extended/trace/cnv",
]

PROBE_HEADERS = [
    "los_config.h",
    "los_task_pri.h",
    "los_sem_pri.h",
    "los_mux_pri.h",
    "los_rwsem_pri.h",
    "los_swtmr_pri.h",
    "los_queue.h",
    "los_cpup.h",
    "los_cpup_pri.h",
]


def mangle(prefix, name):
    return "%s__%s__m" % (prefix, re.sub(r"\W+", "_", name))


CC_WRAPPERS = ("ccache", "distcc", "sccache")


def _cc_matches_arch(cc, macros):
    """True if the compiler name is plausible for the target arch.

    Guards the build-metadata lookup against picking up a host or
    otherwise foreign compiler: a wrong pick must fail closed into
    the PATH-based fallback, never feed the probe.
    """
    cpu = str(macros.get("LOSCFG_ARCH_CPU", "")).lower()
    base = Path(cc).name.lower()
    if macros.get("LOSCFG_ARCH_RISCV") or "riscv" in cpu:
        return "riscv" in base
    if macros.get("LOSCFG_ARCH_ARM") or "cortex" in cpu or "arm" in cpu:
        return "arm" in base
    if "csky" in cpu:
        return "csky" in base
    return True


def _tree_roots(build_dir):
    """Deduped candidate OHOS source-tree roots for the toolchain search."""
    roots = []
    if build_dir:
        p = Path(build_dir)
        if len(p.parents) >= 3:      # out/<board>/<product> -> tree root
            roots.append(p.parents[2])
    roots.append(REPO_ROOT.parent.parent)   # tool repo inside a full tree
    return list(OrderedDict.fromkeys(roots))


def _walk_bin_hits(base, name):
    """Executable <name> hits under bin/ dirs of `base` (unsorted)."""
    hits = []
    for dirpath, dirnames, filenames in os.walk(str(base)):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        if "bin" != os.path.basename(dirpath):
            continue
        p = os.path.join(dirpath, name)
        if name in filenames and os.access(p, os.X_OK):
            hits.append(p)
    return hits


def _search_toolchain(name, build_dir):
    """Find <name> under device/soc / prebuilts of the OHOS source tree.

    SoC SDK toolchains (e.g. ws63's riscv32-linux-musl-gcc) live inside
    the source tree and are only put on PATH by the build environment;
    an interactive shell never sees them. Deterministic first hit wins:
    fp/non-fp are same-version copies of one GCC and are equivalent
    under the probe's explicit -march/-mabi flags.
    """
    for root in _tree_roots(build_dir):
        for sub in ("device/soc", "prebuilts"):
            base = root / sub
            if not base.is_dir():
                continue
            hits = _walk_bin_hits(base, name)
            if hits:
                return sorted(hits)[0]
    return None


def find_build_cc(build_dir, macros, cc_arg=None):
    """Pick the cross compiler for the compile probe.

    Priority: an explicit --cc, then the compiler the build itself used
    (gn emits exactly one ``rule cc`` in toolchain.ninja whose command
    line starts with it, possibly behind a ccache/distcc wrapper; the
    name is resolved against PATH, then the source tree for SoC SDK
    toolchains). Any hiccup -- missing file, format drift, arch
    mismatch, binary nowhere to be found -- returns None so the caller
    fails closed with an actionable error.
    """
    if cc_arg:
        return cc_arg
    if not build_dir:
        return None
    try:
        text = (Path(build_dir) / "toolchain.ninja").read_text(
            encoding="utf-8", errors="replace")
    except OSError:
        return None
    m = re.search(r"^rule cc\n  command = (.+)$", text, re.M)
    if not m:
        return None
    cc = next((t for t in m.group(1).split()
               if Path(t).name not in CC_WRAPPERS), None)
    if cc is None:
        return None
    if not _cc_matches_arch(cc, macros):
        return None
    if os.path.isabs(cc):
        return cc if os.access(cc, os.X_OK) else None
    w = shutil.which(cc)
    if w:
        return w
    return _search_toolchain(Path(cc).name, build_dir)


def arch_flags(macros):
    cpu = str(macros.get("LOSCFG_ARCH_CPU", ""))
    if "cortex" in cpu:
        return ["-mcpu=%s" % cpu, "-mthumb"]
    if macros.get("LOSCFG_ARCH_RISCV") or "riscv" in cpu:
        return ["-march=rv32imac", "-mabi=ilp32"]
    if macros.get("LOSCFG_ARCH_ARM"):
        return ["-mthumb"]
    return []


def _glob_cmsis_dirs(root, pats, dirs):
    for pat in pats:
        for d in root.glob(pat):
            if d.is_dir():
                dirs[str(d)] = True


def find_cmsis_core_dirs():
    """Locate vendor CMSIS Core include dirs (core_cmX.h) near the repo."""
    roots = [REPO_ROOT, REPO_ROOT.parent.parent]
    pats = ("third_party/cmsis*/CMSIS*/Core*/Include",
            "third_party/CMSIS*/Core*/Include",
            "third_party/cmsis*/Include",
            "open_source/CMSIS*/CMSIS*/Core*/Include")
    dirs = OrderedDict()
    for root in roots:
        if not root.is_dir():
            continue
        _glob_cmsis_dirs(root, pats, dirs)
    return list(dirs)


def arch_include_dirs(macros):
    """Arch-specific include dirs for the probe (derived from .config arch)."""
    a = REPO_ROOT / "arch"
    cpu = str(macros.get("LOSCFG_ARCH_CPU", ""))
    cands = []
    if "cortex-m" in cpu:
        cands += [a / "arm" / cpu / "gcc", a / "arm" / cpu,
                  a / "arm" / "cortex_m", a / "arm" / "common",
                  a / "arm" / "include", a / "include"]
    elif "cortex" in cpu:
        cands += [a / "arm" / cpu / "gcc", a / "arm" / cpu,
                  a / "arm" / "cortex_a_r", a / "arm" / "common",
                  a / "arm" / "include", a / "include"]
    if macros.get("LOSCFG_ARCH_RISCV") or "riscv" in cpu:
        cands += [a / "risc-v" / "include", a / "risc-v" / "include" / "arch",
                  a / "risc-v" / "nuclei" / "gcc", a / "risc-v", a / "include"]
    if "csky" in cpu:
        cands += [a / "csky" / "v2" / "gcc", a / "csky" / "v2",
                  a / "csky", a / "include"]
    if macros.get("LOSCFG_ARCH_ARM") and not cands:
        cands += [a / "arm" / "include", a / "include"]
    if not cands:
        cands += [a / "include"]
    return [str(d) for d in cands if d.is_dir()]


def _securec_dirs():
    """Include dirs providing securec.h for module-private header chains.

    los_hpl_common_pri.h includes "securec.h"; the canonical
    bounds_checking_function copy lives in the full OHOS tree
    (third_party/...), while a standalone kernel-repo checkout falls
    back to the mica-bundled copy. Returns absolute Paths (may be empty).
    """
    for root in _tree_roots(None):
        cand = root / "third_party" / "bounds_checking_function" / "include"
        if (cand / "securec.h").is_file():
            return [cand]
    cand = REPO_ROOT / "compat/mcs/mica/platform"
    if (cand / "securec.h").is_file():
        return [cand]
    return []


def _write_cfg_macro(f, k, v):
    if isinstance(v, int):
        f.write("#define %s %d\n" % (k, v))
    elif isinstance(v, str) and not re.match(r"^-?\d+$", v):
        f.write('#define %s "%s"\n' % (k, v.replace('"', "")))


def _write_probe_cfg(cfg, macros):
    with open(cfg, "w") as f:
        f.write("/* generated by footprint */\n")
        for k, v in macros.items():
            _write_cfg_macro(f, k, v)


def _write_probe_c(probe, sizeof_types, macro_names, extra_headers):
    with open(probe, "w") as f:
        f.write("/* generated by footprint */\n")
        for h in PROBE_HEADERS:
            f.write("#include \"%s\"\n" % h)
        # per-anchor module-private headers (repo-relative paths)
        for h in extra_headers:
            f.write("#include \"%s\"\n" % h)
        for t in sorted(sizeof_types):
            f.write("const char %s[sizeof(%s)];\n" % (mangle("sz", t), t))
        for n in sorted(macro_names):
            f.write("#ifdef %s\nconst char %s[(%s)];\n#endif\n"
                    % (n, mangle("v", n), n))


def _probe_include_flags(board_dirs, macros):
    """-I flags for the probe: board dirs, CMSIS Core, arch and kernel
    header dirs, mirroring the kernel's real header environment."""
    cmd = []
    for d in board_dirs:
        cmd.append("-I%s" % d)
        inc_sub = Path(d) / "include"
        if inc_sub.is_dir():
            cmd.append("-I%s" % inc_sub)
    for d in find_cmsis_core_dirs():
        cmd.append("-I%s" % d)
    for d in arch_include_dirs(macros):
        cmd.append("-I%s" % d)
    for inc in PROBE_INCLUDES:
        cmd.append("-I%s" % (REPO_ROOT / inc))
    return cmd


def _probe_compile_cmd(cc, workdir, board_dirs, macros, extra_headers):
    cmd = [cc, "-c", str(workdir / "probe.c"), "-o", str(workdir / "probe.o"),
           "-include", str(workdir / "probe_cfg.h"), "-I%s" % workdir]
    if extra_headers:
        cmd.append("-I%s" % REPO_ROOT)   # resolve repo-relative includes
        for d in _securec_dirs():        # securec.h for private chains
            cmd.append("-I%s" % d)
    # mirror the kernel's real header environment (build.ninja cflags):
    # strict c99 keeps newlib from declaring its POSIX dprintf (which
    # clashes with los_printf.h's VOID dprintf when a module-private
    # header chain pulls in <stdio.h>), fms-extensions keeps anonymous
    # struct/union members parsable
    cmd += ["-std=c99", "-fms-extensions"]
    cmd += _probe_include_flags(board_dirs, macros)
    cmd += arch_flags(macros)
    return cmd


def _resolve_nm(cc):
    nm_name = Path(cc).name.replace("gcc", "nm")
    nm_side = Path(cc).parent / nm_name
    return str(nm_side) if nm_side.exists() else (
        shutil.which(nm_name) or shutil.which("nm"))


def _parse_nm_sizes(stdout):
    values = {}
    for line in stdout.splitlines():
        parts = line.split()
        if len(parts) == 4:
            try:
                values[parts[3]] = int(parts[1], 16)
            except ValueError:
                pass
    return values


def _nm_probe_results(values, sizeof_types, macro_names):
    result = {}
    for t in sizeof_types:
        v = values.get(mangle("sz", t))
        if v is not None:
            result["sz:" + t] = v
    for n in macro_names:
        v = values.get(mangle("v", n))
        if v is not None:
            result["mac:" + n] = v
    return result


def run_probe(cc, macros, sizeof_types, macro_names, workdir, board_dirs,
              extra_headers=()):
    """Compile the probe and return {'sz:TYPE': v, 'mac:NAME': v}."""
    _write_probe_cfg(workdir / "probe_cfg.h", macros)
    _write_probe_c(workdir / "probe.c", sizeof_types, macro_names,
                   extra_headers)
    # los_config.h includes "target_config.h": use the detected board
    # header if available, otherwise provide an empty stub
    if not board_dirs:
        (workdir / "target_config.h").write_text(
            "/* generated by footprint: empty board config stub */\n")
    out = workdir / "probe.o"
    cmd = _probe_compile_cmd(cc, workdir, board_dirs, macros, extra_headers)
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       universal_newlines=True, timeout=120)
    if r.returncode != 0:
        return None, r.stderr.strip()[-1500:]
    nm = _resolve_nm(cc)
    if nm is None:
        return None, "no nm found for %s" % cc
    r = subprocess.run([nm, "-S", str(out)], stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, universal_newlines=True,
                       timeout=60)
    if r.returncode != 0:
        return None, r.stderr.strip()
    return _nm_probe_results(_parse_nm_sizes(r.stdout),
                             sizeof_types, macro_names), None
