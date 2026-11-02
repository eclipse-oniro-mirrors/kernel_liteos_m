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
"""RISC-V 反汇编解析模块(含 RVC/Nuclei Zc):栈帧开销计算与调用关系提取。"""

import re

from asm_common import match_first, parse_disasm, released_at

# ---- 架构专有正则 ----

# __riscv_save_* / __riscv_restore_* 辅助函数
RISCV_SAVE_RE = re.compile(r'^__riscv_save_(\d+)$')
RISCV_RESTORE_RE = re.compile(r'^__riscv_restore_\d+$')

RISCV_SAVE_COST = {
    0: 16,
    4: 64,
    10: 64,
    12: 64,
}

ADDI_SP_RE = re.compile(r'^addi\s+sp,\s*sp,\s*(-?\d+)$')
C_ADDI16SP_RE = re.compile(r'^c\.addi16sp(?:\s+sp)?\s*,?\s*(-?\d+)$')
C_ADDI_SP_RE = re.compile(r'^c\.addi\s+sp\s*,\s*(-?\d+)$')
# c.push/c.pop (Nuclei Zc 扩展)
C_PUSH_RE = re.compile(r'^c\.push(?:\s+sp)?\s*,?\s*(-?\d+)$')
C_POP_RE = re.compile(r'^c\.pop(?:\s+sp)?\s*,?\s*(-?\d+)$')

# c.addi4spn 是纯取址指令(rd = sp + imm,rd 只能是 x8-x15,不可能写 sp),不计入 frame_size


def _sp_delta(mnemonic, operands):
    """单条指令对 sp 净增量,语义同 ARM 版:正=分配 / 负=恢复 / 0=不动 / None=未知(保守)。"""
    full = mnemonic + (' ' + operands if operands else '')

    m = match_first(full, (ADDI_SP_RE, C_ADDI16SP_RE, C_ADDI_SP_RE))
    if m:
        return -int(m.group(1))
    m = C_PUSH_RE.match(full)
    if m:
        n = int(m.group(1))
        return -n if n < 0 else None
    m = C_POP_RE.match(full)
    if m:
        n = int(m.group(1))
        return -n if n > 0 else None

    # __riscv_save_*:栈分配(助记符形式或 jal 注解形式)
    m = RISCV_SAVE_RE.match(mnemonic)
    if m:
        return RISCV_SAVE_COST.get(int(m.group(1)), 64)
    if mnemonic == 'jal':
        toks = operands.split(',')[-1].strip().split()
        if len(toks) > 1:
            m = RISCV_SAVE_RE.match(toks[1].strip('<>'))
            if m:
                return RISCV_SAVE_COST.get(int(m.group(1)), 64)
            if RISCV_RESTORE_RE.match(toks[1].strip('<>')):
                return None
    # __riscv_restore_* 后代码不可达 / mv sp,reg 帧指针恢复:保守视为未知
    if RISCV_RESTORE_RE.match(mnemonic):
        return None
    if mnemonic in ('mv', 'c.mv') and operands.strip().startswith('sp,'):
        return None
    return 0


def _jal_parts(mnemonic, operands):
    """解析 jal/c.jal 操作数 → (目标地址, 注释符号, rd);rd 省略按 x0,c.jal 隐含 ra。"""
    parts = [p.strip() for p in operands.split(',')]
    toks = parts[-1].split()
    tgt = toks[0] if toks else ''
    ann = toks[1].strip('<>') if len(toks) > 1 else ''
    rd = parts[0] if len(parts) > 1 else 'x0'
    if mnemonic == 'c.jal':
        rd = 'ra'
    return tgt, ann, rd


def _jalr_info(operands):
    """解析 jalr/c.jalr 操作数 → (基址寄存器, 是否纯返回形式 jalr ra/0(ra))。"""
    m = re.search(r'\((\w+)\)', operands)
    parts = [p.strip() for p in operands.split(',') if p.strip()]
    if m:
        base = m.group(1)
    else:
        base = parts[-1] if parts else ''
    is_ret = (base == 'ra') and all(
        p in ('ra', '0(ra)', '0') for p in parts)
    return base, is_ret


def _stack_effect(mnemonic, full):
    """返回指令对函数帧的增量(入栈/下调为正);非栈操作返回 None。"""
    m = RISCV_SAVE_RE.match(mnemonic)
    if m:
        return RISCV_SAVE_COST.get(int(m.group(1)), 64)
    if RISCV_RESTORE_RE.match(mnemonic):
        return 0
    m = match_first(full, (ADDI_SP_RE, C_ADDI16SP_RE,
                           C_ADDI_SP_RE, C_PUSH_RE))
    if m:
        n = int(m.group(1))
        return -n if n < 0 else 0
    return None


def _call_effect(mnemonic, operands, instr_addr, instructions, idx, frame_size):
    """jal/c.jal 直连 → 普通/尾调用(__riscv_save/restore 目标只产生帧增量,
    无调用边);jalr/jr 非返回形式 → 间接调用;条件分支/j 跳转点帧已
    释放 → 尾调用,否则按普通调用(保守)。"""
    frame_delta = 0
    callees = set()
    tails = set()
    ptr_loc = None
    ptr_var = None

    if mnemonic in ('jal', 'c.jal'):
        tgt, ann, rd = _jal_parts(mnemonic, operands)
        m = RISCV_SAVE_RE.match(ann)
        if m:
            frame_delta = RISCV_SAVE_COST.get(int(m.group(1)), 64)
        elif not RISCV_RESTORE_RE.match(ann) and tgt:
            # rd 省略或 x0 → 尾调用候选;帧未释放仍按普通调用叠加(保守)
            if rd in ('x0', 'zero') and released_at(instructions, idx, frame_size, _sp_delta):
                tails.add(tgt)
            else:
                callees.add(tgt)
    elif mnemonic in ('jalr', 'c.jalr'):
        base, is_ret = _jalr_info(operands)
        if not is_ret:
            ptr_loc = 'jalr {} @0x{:x}'.format(base, instr_addr)
            ptr_var = base
    elif mnemonic in ('jr', 'c.jr'):
        base = operands.strip().split(',')[0].strip()
        if base and base != 'ra':
            ptr_loc = 'jr {} @0x{:x}'.format(base, instr_addr)
            ptr_var = base
    elif mnemonic in ('beq', 'bne', 'blt', 'bge', 'bltu', 'bgeu',
                      'beqz', 'bnez', 'bltz', 'bgez', 'blez', 'bgtz'):
        # 仅函数起始目标会在主脚本 _resolve_callees 中保留
        toks = operands.split(',')[-1].strip().split()
        if toks:
            callees.add(toks[0])
    elif mnemonic in ('j', 'c.j'):
        toks = operands.strip().split()
        if toks:
            if released_at(instructions, idx, frame_size, _sp_delta):
                tails.add(toks[0])
            else:
                callees.add(toks[0])
    return frame_delta, callees, tails, ptr_loc, ptr_var


def parse_function(name, addr, instructions):
    """返回 (frame_size, callee_addrs, tail_callee_addrs, ptr_src_location,
    ptr_var_name);尾调用边(callee 复用已释放栈空间)估值不叠加本帧。"""
    frame_size = 0
    callee_addrs = set()
    tail_callee_addrs = set()
    ptr_src_location = []
    ptr_var_name = []

    for idx, (instr_addr, mnemonic, operands) in enumerate(instructions):
        full = mnemonic
        if operands:
            full = mnemonic + ' ' + operands

        d = _stack_effect(mnemonic, full)
        if d is not None:
            frame_size += d
            continue

        fd, callees, tails, ptr_loc, ptr_var = _call_effect(
            mnemonic, operands, instr_addr, instructions, idx, frame_size)
        frame_size += fd
        callee_addrs |= callees
        tail_callee_addrs |= tails
        if ptr_loc is not None:
            ptr_src_location.append(ptr_loc)
            ptr_var_name.append(ptr_var)

    return frame_size, callee_addrs, tail_callee_addrs, ptr_src_location, ptr_var_name


def detect(lines):
    for line in lines[:50]:
        if 'riscv' in line.lower():
            return True
    return False
