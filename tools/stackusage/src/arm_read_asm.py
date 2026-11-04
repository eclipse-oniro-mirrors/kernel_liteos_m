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
"""ARM 反汇编解析模块(Thumb/Thumb-2):栈帧开销计算与调用关系提取。"""

import re

from asm_common import match_first, parse_disasm, released_at

# ---- 架构专有正则 ----

PUSH_RE = re.compile(r'^push(?:\.w|\.n)?\s+\{([^}]+)\}')

STMDB_RE = re.compile(r'^stmdb(?:\.w)?\s+sp\s*!,\s*\{([^}]+)\}')

# str/strd rX[, rY], [sp, #-N]! 前变址压栈,N 即 sp 下调量
STR_PRE_RE = re.compile(
    r'^strd?(?:\.w|\.n)?\s+\w+(?:\s*,\s*\w+)?,\s*\[sp\s*,\s*#-(\d+)\]!')

VPUSH_RE = re.compile(r'^vpush(?:\.\d+)?\s+\{([^}]+)\}')
VPOP_RE = re.compile(r'^vpop(?:\.\d+)?\s+\{([^}]+)\}')

SUB_SP_RE = re.compile(r'^subw?\s+sp\s*,\s*(?:sp\s*,\s*)?#(\d+)')

# bl <addr> [<symbol>](可带条件码;blt/bge 等不会误配:bl 后仅剩 1 字母构不成条件码)
BL_RE = re.compile(
    r'^bl(?:eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al|nv|hs|lo)?'
    r'(?:\.w|\.n)?\s+(\S+)')

# blx <addr> [<symbol>] / blx rN (可带条件码)
BLX_RE = re.compile(
    r'^blx(?:eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al|nv|hs|lo)?'
    r'(?:\.w|\.n)?\s+(\S+)')

REG_NAME_RE = re.compile(r'^(r\d+|lr|pc|ip|sp|fp|sl|sb)$', re.IGNORECASE)

B_RE = re.compile(r'^b(?:\.w|\.n)?\s+(\S+)')
BCC_RE = re.compile(r'^b(?:eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al|nv|hs|lo)(?:\.w|\.n)?\s+(\S+)')

# ---- 以下正则用于尾调用判定的后向扫描(sp 恢复指令识别) ----

POP_RE = re.compile(r'^pop(?:\.w|\.n)?\s+\{([^}]+)\}')

LDMIA_RE = re.compile(r'^ldmia(?:\.w)?\s+sp\s*!,\s*\{([^}]+)\}')

ADD_SP_RE = re.compile(r'^addw?\s+sp\s*,\s*(?:sp\s*,\s*)?#(\d+)')

LDR_POST_RE = re.compile(r'^ldrw?\s+\w+,\s*\[sp\]\s*,\s*#(\d+)')

# 未精确解析的 sp 写入(后向扫描保守视为未知)
MOV_SP_RE = re.compile(r'^movw?(?:\.w|\.n)?\s+sp\s*,')
SUB_SP_ANY_RE = re.compile(r'^subw?\s+sp\s*,')
ADD_SP_ANY_RE = re.compile(r'^addw?\s+sp\s*,')


def _strip_suffix(mnemonic):
    if mnemonic.endswith('.w') or mnemonic.endswith('.n'):
        return mnemonic[:-2]
    return mnemonic


# ARM 条件码(IT 块内条件执行:subeq sp,popne 等)
_COND_CODES = ('eq', 'ne', 'cs', 'cc', 'mi', 'pl', 'vs', 'vc',
               'hi', 'ls', 'ge', 'lt', 'gt', 'le', 'al', 'nv', 'hs', 'lo')


def _strip_cond(mnemonic):
    """去除 .w/.n 与条件码后缀(subeq.w → sub);movs 等误剥结果不匹配任何栈操作正则,行为不变。"""
    m = _strip_suffix(mnemonic)
    for cc in _COND_CODES:
        if len(m) > len(cc) and m.endswith(cc):
            return m[:-len(cc)]
    return m


def _count_regs(reg_str):
    """计算寄存器列表的寄存器数量(支持 r4-r11 区间)。"""
    count = 0
    for part in reg_str.split(','):
        part = part.strip()
        if not part:
            continue
        if '-' in part:
            m = re.match(r'r(\d+)-r(\d+)', part, re.IGNORECASE)
            if m:
                start = int(m.group(1))
                end = int(m.group(2))
                count += end - start + 1
            else:
                count += 1
        else:
            count += 1
    return count


def _vfp_bytes(reg_str):
    """计算 vpush/vpop 列表字节数:d 系 8 字节、s 系 4 字节。"""
    total = 0
    for part in reg_str.split(','):
        part = part.strip()
        if not part:
            continue
        m = re.match(r'([ds])(\d+)-[ds](\d+)$', part, re.IGNORECASE)
        if m:
            n = int(m.group(3)) - int(m.group(2)) + 1
            total += n * (8 if m.group(1).lower() == 'd' else 4)
            continue
        m = re.match(r'([ds])(\d+)$', part, re.IGNORECASE)
        if m:
            total += 8 if m.group(1).lower() == 'd' else 4
    return total


def _sp_delta(mnemonic, operands):
    """单条指令对 sp 净增量:正=分配 / 负=恢复 / 0=不动 / None=未知。

    None(未精确解析或条件执行的恢复)保守视为帧状态未知;
    条件执行的分配保守计入(帧只可能更大)。
    """
    full = mnemonic + (' ' + operands if operands else '')
    toks = full.split(None, 1)
    if not toks:
        return 0
    bare = _strip_cond(toks[0])
    was_cond = bare != _strip_suffix(toks[0])
    ops = toks[1] if len(toks) > 1 else ''
    mfull = bare + (' ' + ops if ops else '')

    # 分配方向(条件形式也保守计入)
    m = match_first(mfull, (PUSH_RE, STMDB_RE))
    if m:
        return _count_regs(m.group(1)) * 4
    m = STR_PRE_RE.match(mfull)
    if m:
        return int(m.group(1))
    m = VPUSH_RE.match(mfull)
    if m:
        return _vfp_bytes(m.group(1))
    m = SUB_SP_RE.match(mfull)
    if m:
        return int(m.group(1))

    # 恢复方向(条件执行时保守返回 None)
    m = match_first(mfull, (POP_RE, LDMIA_RE))
    if m:
        if was_cond:
            return None
        return -_count_regs(m.group(1)) * 4
    m = VPOP_RE.match(mfull)
    if m:
        if was_cond:
            return None
        return -_vfp_bytes(m.group(1))
    m = match_first(mfull, (ADD_SP_RE, LDR_POST_RE))
    if m:
        if was_cond:
            return None
        return -int(m.group(1))

    if MOV_SP_RE.match(mfull) or SUB_SP_ANY_RE.match(mfull) \
            or ADD_SP_ANY_RE.match(mfull):
        return None
    return 0


def _stack_effect(sfull):
    """返回指令对函数帧的增量(压栈/下调为正);非栈操作返回 None。"""
    m = match_first(sfull, (PUSH_RE, STMDB_RE))
    if m:
        return _count_regs(m.group(1)) * 4
    m = STR_PRE_RE.match(sfull)
    if m:
        return int(m.group(1))
    m = VPUSH_RE.match(sfull)
    if m:
        return _vfp_bytes(m.group(1))
    m = SUB_SP_RE.match(sfull)
    if m:
        return int(m.group(1))
    return None


def _call_effect(mnemonic, operands, instr_addr, instructions, idx, frame_size):
    """bl/blx 直连目标 → 普通调用;blx rN → 间接调用;
    b/bcc 跳转点帧已释放 → 尾调用,否则按普通调用(保守)。"""
    callees = set()
    tails = set()
    ptr_loc = None
    ptr_var = None
    full = _strip_suffix(mnemonic)
    if operands:
        full = full + ' ' + operands

    m = BL_RE.match(full)
    if m:
        callees.add(m.group(1))
        return callees, tails, ptr_loc, ptr_var
    m = BLX_RE.match(full)
    if m:
        target = m.group(1)
        if REG_NAME_RE.match(target):
            ptr_loc = 'blx {} @0x{:x}'.format(target, instr_addr)
            ptr_var = target
        else:
            callees.add(target)
        return callees, tails, ptr_loc, ptr_var
    m = match_first(full, (B_RE, BCC_RE))
    if m:
        if released_at(instructions, idx, frame_size, _sp_delta):
            tails.add(m.group(1))
        else:
            callees.add(m.group(1))
    return callees, tails, ptr_loc, ptr_var


def parse_function(name, addr, instructions):
    """返回 (frame_size, callee_addrs, tail_callee_addrs, ptr_src_location,
    ptr_var_name);尾调用边(callee 复用已释放栈空间)估值不叠加本帧。"""
    frame_size = 0
    callee_addrs = set()
    tail_callee_addrs = set()
    ptr_src_location = []
    ptr_var_name = []

    for idx, (instr_addr, mnemonic, operands) in enumerate(instructions):
        # 栈操作匹配用条件码剥离形式(subeq.w sp → sub sp),
        # 调用/跳转匹配用宽度后缀剥离形式(不含条件码)
        sfull = _strip_cond(mnemonic)
        if operands:
            sfull = sfull + ' ' + operands

        d = _stack_effect(sfull)
        if d is not None:
            frame_size += d
            continue

        callees, tails, ptr_loc, ptr_var = _call_effect(
            mnemonic, operands, instr_addr, instructions, idx, frame_size)
        callee_addrs |= callees
        tail_callee_addrs |= tails
        if ptr_loc is not None:
            ptr_src_location.append(ptr_loc)
            ptr_var_name.append(ptr_var)

    return frame_size, callee_addrs, tail_callee_addrs, ptr_src_location, ptr_var_name


def detect(lines):
    for line in lines[:50]:
        if 'elf32-littlearm' in line.lower() or 'elf64-littleaarch64' in line.lower():
            return True
    return False
