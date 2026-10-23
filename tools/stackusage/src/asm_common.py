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
"""反汇编解析公共模块(架构无关):objdump 状态机 parse_disasm、
尾调用帧释放判定 released_at;架构专有语义由 arm/riscv_read_asm 提供。"""

import re

# ---- objdump 公共输出格式 ----

FUNC_LABEL_RE = re.compile(r'^\s*([0-9a-f]+)\s+<([^>]+)>:\s*$')

INSTR_RE = re.compile(r'^\s*([0-9a-f]+):\s+([0-9a-f ]+?)\t+(\S+)\s*(.*)$')

INSTR_PLAIN_RE = re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\t?(.*)$')

# objdump -l 行号标记:/path/to/file.c:123(路径不可含空白与冒号)
LINE_MARKER_RE = re.compile(r'^\s*([^\s:]+\.[A-Za-z0-9]+):(\d+)\s*$')

FILE_HEADER_RE = re.compile(r'^\S+:\s+file format ')


def match_first(text, regexes):
    for rx in regexes:
        m = rx.match(text)
        if m:
            return m
    return None


def released_at(instructions, idx, frame_size, sp_delta):
    """后向扫描判定:idx 处的跳转/调用发生时,栈帧是否已完全释放(真尾调用)。

    从跳转指令往回累计 sp 恢复量,凑满 -frame_size 即已释放;途中遇
    分配指令即未释放;无法精确解析的栈操作保守判未释放。sp_delta 为
    架构专有的单指令 sp 净增量:正=分配 / 负=恢复 / 0=不动 / None=未知。
    """
    cum = 0
    for j in range(idx - 1, -1, -1):
        _, mnemonic, operands = instructions[j]
        d = sp_delta(mnemonic, operands)
        if d is None:
            return False
        if d == 0:
            continue
        cum += d
        if d > 0:
            return False
        if cum <= -frame_size:
            return True
    return frame_size == 0


def classify_line(line, stripped, mode):
    """非指令行分类:marker(-l 行号标记)/ src(-S 源码行)/ blank / skip / other。"""
    if mode >= 1:
        m = LINE_MARKER_RE.match(line)
        if m:
            return 'marker', (m.group(1), int(m.group(2)))
    if stripped == '':
        return 'blank', None
    if stripped == '...' or FILE_HEADER_RE.match(stripped) \
            or stripped.startswith('Disassembly of section'):
        return 'skip', None
    if mode >= 2:
        return 'src', stripped
    return 'other', None


def parse_disasm(lines, mode=0):
    """返回 (functions, line_map, src_map);line_map/src_map 仅在 mode >= 1/>= 2 时记录。"""
    functions = {}
    line_map = {}
    src_map = {}
    current_name = None
    current_addr = None
    current_instrs = []
    cur_marker = None
    cur_src = None

    def flush():
        if current_addr is not None:
            functions[current_addr] = (current_name, current_addr, current_instrs)

    def record_maps(instr_addr, marker, src):
        if mode >= 1 and marker is not None:
            line_map[instr_addr] = marker
        if mode >= 2 and src is not None:
            src_map[instr_addr] = src

    for line in lines:
        m = FUNC_LABEL_RE.match(line)
        if m:
            flush()
            current_addr = int(m.group(1), 16)
            current_name = m.group(2)
            current_instrs = []
            # 标记/源码行状态在函数边界重置,避免归属到上一函数
            cur_marker = None
            cur_src = None
            continue

        m = INSTR_RE.match(line)
        if m and current_addr is not None:
            instr_addr = int(m.group(1), 16)
            mnemonic = m.group(3)
            operands = m.group(4).strip()
            current_instrs.append((instr_addr, mnemonic, operands))
            record_maps(instr_addr, cur_marker, cur_src)
            continue

        m = INSTR_PLAIN_RE.match(line)
        if m and current_addr is not None:
            instr_addr = int(m.group(1), 16)
            mnemonic = m.group(2)
            operands = m.group(3).strip()
            current_instrs.append((instr_addr, mnemonic, operands))
            record_maps(instr_addr, cur_marker, cur_src)
            continue

        kind, val = classify_line(line, line.strip(), mode)
        if kind == 'marker':
            cur_marker = val
        elif kind == 'src':
            cur_src = val
        elif kind == 'blank' and mode == 0:
            flush()
            current_addr = None
            current_name = None
            current_instrs = []

    flush()
    return functions, line_map, src_map
