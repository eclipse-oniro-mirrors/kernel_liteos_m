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
"""stackusage.py - 静态栈使用量分析工具(用法见 README.md,算法见 stackusage_design.md)。"""

import argparse
import json
import os
import re
import sys

# 从源码行提取函数指针变量名(取尾段标识符:g_hook(x) → g_hook)
_PTR_DEREF_CALL_RE = re.compile(r'\(\s*\*\s*(\w+)\s*\)\s*\(')
_PTR_PLAIN_CALL_RE = re.compile(r'(\w+)\s*\(')
_PTR_CALL_KEYWORDS = {'if', 'for', 'while', 'switch', 'sizeof', 'return'}
# 间接调用站点串中的指令地址:"blx r3 @0x21020f6a"
_SITE_ADDR_RE = re.compile(r'@0x([0-9a-f]+)')
# 调用树渲染深度上限(根为第 0 层,仅影响树展示)
MAX_TREE_DEPTH = 15

def extract_ptr_var_name(src_line):
    """从源码行提取被调用的函数指针变量名,失败返回 None。"""
    if not src_line:
        return None
    candidates = []
    for m in _PTR_DEREF_CALL_RE.finditer(src_line):
        candidates.append((m.start(), m.group(1)))
    for m in _PTR_PLAIN_CALL_RE.finditer(src_line):
        tok = m.group(1)
        if tok in _PTR_CALL_KEYWORDS:
            continue
        candidates.append((m.start(), tok))
    if not candidates:
        return None
    return max(candidates)[1]


class Function:
    def __init__(self, addr, name):
        self.addr = addr
        self.name = name
        self.instructions = []
        self.frame_size = 0
        self.max_size = 0
        self.callee_addrs = set()
        self.tail_callee_addrs = set()
        self.ptr_src_location = []
        self.ptr_var_name = []
        self.is_called = False
        self.is_rec = False
        self.is_ignore = False
        self.location = ''
        self.depth = 0
        self.chain = []
        self.manual_callees = set()


class StackAnalyzer:
    def __init__(self, arch, rec_depth=1):
        self.arch = arch
        self.rec_depth = rec_depth
        self.functions = {}
        # 同名函数多实例(内联/克隆、不同编译单元的 static 函数),按名查找需展开到全部实例
        self.name_to_addrs = {}
        self.ignore_set = set()
        self.interrupt_entries = []
        self.interrupt_isrs = {}
        self.rec_cycles = []
        self.miss_funcptr = []
        self.config_path = './cfg/config.ini'
        # -m 分析模式:0=仅地址;1=源码行号(-l);2=行号+源码行(-S)
        self.mode = 0
        self.line_map = {}
        self.src_map = {}

    def parse_disasm(self, asm_file):
        with open(asm_file, 'r', errors='replace') as f:
            lines = f.readlines()

        if self.arch == 'arm':
            import arm_read_asm
            raw, line_map, src_map = arm_read_asm.parse_disasm(lines, self.mode)
        else:
            import riscv_read_asm
            raw, line_map, src_map = riscv_read_asm.parse_disasm(lines, self.mode)
        self.line_map = line_map
        self.src_map = src_map

        for addr, (name, _, instrs) in raw.items():
            func = Function(addr, name)
            func.instructions = instrs
            if self.arch == 'arm':
                import arm_read_asm
                fs, callees, tails, ptrs, ptrs_var = \
                    arm_read_asm.parse_function(name, addr, instrs)
            else:
                import riscv_read_asm
                fs, callees, tails, ptrs, ptrs_var = \
                    riscv_read_asm.parse_function(name, addr, instrs)
            func.frame_size = fs
            func.ptr_src_location = ptrs
            func.ptr_var_name = ptrs_var
            for c in callees:
                func.callee_addrs.add(c)
            for c in tails:
                func.tail_callee_addrs.add(c)
            if self.mode >= 1 and func.instructions:
                # Location = 函数首指令的行号标记(定义行)
                marker = line_map.get(func.instructions[0][0])
                if marker:
                    func.location = '{}:{}'.format(
                        os.path.basename(marker[0]), marker[1])
            if self.mode == 2 and func.ptr_src_location:
                # PtrVarName:逐站点提取,失败的站点丢弃
                names = []
                for loc in func.ptr_src_location:
                    m = _SITE_ADDR_RE.search(loc)
                    if not m:
                        continue
                    var = extract_ptr_var_name(
                        src_map.get(int(m.group(1), 16)))
                    if var:
                        names.append(var)
                func.ptr_var_name = names
            self.functions[addr] = func
            self.name_to_addrs.setdefault(name, []).append(addr)

        self._resolve_callees()
        self._mark_called()

    def _resolve_callees(self):
        """仅保留指向函数起始地址的目标(过滤 <Func+0xN> 局部标号)。"""
        for func in self.functions.values():
            resolved = set()
            for c in func.callee_addrs:
                addr = self._resolve_target(c)
                if addr is not None and addr in self.functions:
                    resolved.add(addr)
            func.callee_addrs = resolved

            resolved_tail = set()
            for c in func.tail_callee_addrs:
                addr = self._resolve_target(c)
                if addr is not None and addr in self.functions:
                    resolved_tail.add(addr)
            func.tail_callee_addrs = resolved_tail

    def _resolve_target(self, target):
        if isinstance(target, int):
            return target
        target = target.strip()
        if target.startswith('<') and target.endswith('>'):
            target = target[1:-1]
        if target.startswith('0x') or target.startswith('0X'):
            try:
                return int(target, 16)
            except ValueError:
                return None
        try:
            return int(target, 16)
        except ValueError:
            pass
        # 符号名回退(防御性):同名多实例取最小地址
        addrs = self.name_to_addrs.get(target)
        if addrs:
            return min(addrs)
        return None

    def _mark_called(self):
        for func in self.functions.values():
            for callee_addr in func.callee_addrs | func.tail_callee_addrs:
                if callee_addr in self.functions:
                    self.functions[callee_addr].is_called = True

    def load_config(self, config_path=None):
        if config_path:
            self.config_path = config_path
        if not os.path.exists(self.config_path):
            return

        with open(self.config_path, 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue

                if line.startswith('Miss_funcptr:'):
                    continue
                if line.startswith('Remarks_here:'):
                    self._load_remarks_line(line)
                elif line.startswith('Ignore_function:'):
                    self._load_ignore_line(line)
                elif line.startswith('Interrupt_entry:'):
                    self._load_interrupt_entry_line(line)
                elif line.startswith('Interrupt_isr:'):
                    self._load_interrupt_isr_line(line)

    def _load_remarks_line(self, line):
        # 兼容 Name:FuncA / <Name>:FuncA / <Name>:0xAddr:FuncA 三种填写形式
        rest = line[len('Remarks_here:'):].strip()
        parts = rest.split(':')
        func_name = None
        callee_names = None
        if len(parts) == 2:
            if not re.match(r'^0x[0-9a-fA-F]+$', parts[1].strip()):
                func_name = parts[0].strip()
                callee_names = parts[1].strip().split()
        elif len(parts) >= 3:
            if re.match(r'^0x[0-9a-fA-F]+$', parts[1].strip()):
                func_name = parts[0].strip()
                callee_names = parts[2].strip().split()
        if func_name is None:
            return
        func_name = func_name.strip('<>')
        # 同名多实例:补全到全部实例(漏补会造成低估)
        for addr in self.name_to_addrs.get(func_name, []):
            func = self.functions[addr]
            for cn in callee_names:
                if cn.upper() == 'NULL':
                    continue
                for ca in self.name_to_addrs.get(cn, []):
                    func.manual_callees.add(ca)
            if func.manual_callees:
                func.callee_addrs |= func.manual_callees
                func.ptr_src_location = []

    def _load_ignore_line(self, line):
        funcs = line[len('Ignore_function:'):].strip().split()
        for fn in funcs:
            for addr in self.name_to_addrs.get(fn, []):
                if addr in self.functions:
                    self.ignore_set.add(addr)
                    f = self.functions[addr]
                    f.is_ignore = True
                    f.frame_size = 0
                    f.callee_addrs = set()
                    f.tail_callee_addrs = set()
                    f.is_called = True
                    f.max_size = 0

    def _load_interrupt_entry_line(self, line):
        funcs = line[len('Interrupt_entry:'):].strip().split()
        for fn in funcs:
            for addr in self.name_to_addrs.get(fn, []):
                self.interrupt_entries.append(addr)

    def _load_interrupt_isr_line(self, line):
        rest = line[len('Interrupt_isr:'):].strip()
        parts = rest.split(':', 1)
        if len(parts) != 2:
            return
        try:
            irq_num = int(parts[0].strip())
        except ValueError:
            return
        isr_addrs = []
        for isr in parts[1].strip().split():
            isr_addrs.extend(self.name_to_addrs.get(isr, []))
        if isr_addrs:
            if irq_num in self.interrupt_isrs:
                self.interrupt_isrs[irq_num].extend(isr_addrs)
            else:
                self.interrupt_isrs[irq_num] = isr_addrs

    def update_config(self):
        """同步重写:用户区(已填 Remarks/Ignore/Interrupt 等)原样保留,
        工具区(Miss_funcptr + 空桩)按当前未解决站点重新生成,幂等。"""
        unresolved_sites = self._collect_unresolved_sites()

        def is_tool_line(line):
            s = line.strip()
            if s.startswith('Miss_funcptr:'):
                return True
            if s.startswith('Remarks_here:'):
                rest = s[len('Remarks_here:'):].strip()
                parts = rest.split(':')
                if len(parts) == 1:
                    return True   # 旧空桩:Remarks_here:Name
                if len(parts) == 2 \
                        and re.match(r'^0x[0-9a-fA-F]+$', parts[1].strip()):
                    return True   # 新空桩:Remarks_here:<Name>:0xAddr
            return False

        user_lines = self._collect_user_lines(is_tool_line)

        if not unresolved_sites and not os.path.exists(self.config_path):
            return

        cfg_dir = os.path.dirname(self.config_path)
        if cfg_dir and not os.path.exists(cfg_dir):
            os.makedirs(cfg_dir, exist_ok=True)

        with open(self.config_path, 'w') as f:
            for line in user_lines:
                f.write(line)
            if user_lines and unresolved_sites:
                f.write('\n')
            for name, addr in unresolved_sites:
                f.write('Miss_funcptr:<{}>:0x{:x}\n'.format(name, addr))
                f.write('Remarks_here:<{}>:0x{:x}\n'.format(name, addr))
                f.write('\n')

    def _collect_user_lines(self, is_tool_line):
        # 剥离工具行及其后的格式空行,避免多次运行累积
        user_lines = []
        if os.path.exists(self.config_path):
            with open(self.config_path, 'r') as f:
                lines = f.readlines()
            i = 0
            while i < len(lines):
                if is_tool_line(lines[i]):
                    i += 1
                    if i < len(lines) and lines[i].strip() == '':
                        i += 1
                    continue
                line = lines[i]
                if not line.endswith('\n'):
                    line += '\n'
                user_lines.append(line)
                i += 1
        while user_lines and user_lines[-1].strip() == '':
            user_lines.pop()
        return user_lines

    def _collect_unresolved_sites(self):
        sites = []
        for func in self.functions.values():
            if func.ptr_src_location and not func.is_ignore:
                for loc in func.ptr_src_location:
                    m = _SITE_ADDR_RE.search(loc)
                    if m:
                        sites.append((func.name, int(m.group(1), 16)))
        return sites

    def enumerate_entries(self, single_point, entry_names):
        if single_point:
            addrs = []
            for name in entry_names:
                found = self.name_to_addrs.get(name)
                if found:
                    addrs.extend(sorted(found))
                else:
                    print('[ERROR] Entry function not found: {}'.format(name), file=sys.stderr)
            return addrs
        else:
            return [addr for addr, f in self.functions.items()
                    if not f.is_called and not f.is_ignore]

    def _tarjan_scc(self):
        """迭代式 Tarjan(边集=普通∪尾调用:混合环仍是无界递归),按逆拓扑序返回。"""
        adj = {a: sorted(f.callee_addrs | f.tail_callee_addrs)
               for a, f in self.functions.items()}
        index = {}
        low = {}
        on_stack = set()
        stack = []
        sccs = []
        counter = 0

        def pop_scc(v):
            comp = []
            while True:
                w = stack.pop()
                on_stack.discard(w)
                comp.append(w)
                if w == v:
                    break
            sccs.append(comp)

        for root in sorted(self.functions):
            if root in index:
                continue
            index[root] = low[root] = counter
            counter += 1
            stack.append(root)
            on_stack.add(root)
            # 显式栈模拟递归:work 每层存 (结点, 其邻接迭代器)
            work = [(root, iter(adj[root]))]
            while work:
                v, it = work[-1]
                advanced = False
                for w in it:
                    if w not in self.functions:
                        continue
                    if w not in index:
                        index[w] = low[w] = counter
                        counter += 1
                        stack.append(w)
                        on_stack.add(w)
                        work.append((w, iter(adj[w])))
                        advanced = True
                        break
                    elif w in on_stack:
                        if index[w] < low[v]:
                            low[v] = index[w]
                if advanced:
                    continue
                work.pop()
                if work:
                    pv = work[-1][0]
                    if low[v] < low[pv]:
                        low[pv] = low[v]
                # low == index:v 为 SCC 根,弹栈到 v 即一个 SCC
                if low[v] == index[v]:
                    pop_scc(v)
        return sccs

    def _cycle_path(self, comp):
        comp_set = set(comp)
        start = min(comp)
        path = [start]
        on_path = {start}

        def walk(v):
            for w in sorted(self.functions[v].callee_addrs |
                            self.functions[v].tail_callee_addrs):
                if w not in comp_set:
                    continue
                if w in on_path:
                    cycle = path[path.index(w):] + [w]
                    return ' -> '.join('<{}>'.format(
                        self.functions[a].name if a in self.functions else '?')
                        for a in cycle)
                path.append(w)
                on_path.add(w)
                r = walk(w)
                if r:
                    return r
                path.pop()
                on_path.discard(w)
            return None

        return walk(start)

    def compute_values(self):
        """SCC 感知的确定估值:val = max(frame + max(普通 callee), max(尾 callee)),
        尾调用帧已释放不叠加本帧;环成员标记 is_rec,帧按 -r N 放大
        (环只展开一遍,重入边不贡献帧)。路径式探索详见设计文档。"""
        sccs = self._tarjan_scc()
        scc_id = {}
        for i, comp in enumerate(sccs):
            for a in comp:
                scc_id[a] = i

        self._mark_rec_cycles(sccs)

        for i, comp in enumerate(sccs):
            if len(comp) == 1:
                f = self.functions[comp[0]]
                if f.is_ignore:
                    f.max_size, f.chain, f.depth = 0, ['*Ignore'], 0
                    continue
                self._value_single(f)
            else:
                self._value_multi_member(comp, scc_id)

    def _mark_rec_cycles(self, sccs):
        for comp in sccs:
            if len(comp) >= 2:
                for a in comp:
                    self.functions[a].is_rec = True
                self.rec_cycles.append(self._cycle_path(comp))
            else:
                a = comp[0]
                f = self.functions[a]
                if a in f.callee_addrs:
                    f.is_rec = True
                    self.rec_cycles.append('<{0}> -> <{0}>'.format(f.name))

    def _value_single(self, f):
        # 自环:N×frame + 其余 callee 最优;尾调用分支不叠加本帧,取 max
        a = f.addr
        best_cost, best_chain = 0, []
        for c in sorted(f.callee_addrs):
            if c == a:
                continue
            cf = self.functions[c]
            if cf.max_size > best_cost:
                best_cost, best_chain = cf.max_size, cf.chain
        if f.is_rec:
            body = self.rec_depth * f.frame_size + best_cost
            body_chain = [f.name + '*Rec'] + best_chain
        else:
            if f.ptr_src_location and not f.manual_callees \
                    and not f.callee_addrs \
                    and not f.tail_callee_addrs:
                best_chain = ['[unresolved]']
            body = f.frame_size + best_cost
            body_chain = [f.name] + best_chain
        tail_cost, tail_chain = 0, None
        for t in sorted(f.tail_callee_addrs):
            if t == a:
                continue
            cf = self.functions[t]
            if cf.max_size > tail_cost:
                tail_cost, tail_chain = cf.max_size, cf.chain
        f.max_size, f.chain = self._pick_tail_or_body(
            f.name, body, body_chain, tail_cost, tail_chain)
        f.depth = len(f.chain) - 1

    def _pick_tail_or_body(self, label, body, body_chain, tail_c, tail_ch):
        if tail_ch is not None and tail_c > body:
            marked = list(tail_ch)
            marked[0] = marked[0] + '*Tail'
            return tail_c, [label] + marked
        return body, body_chain

    def _value_multi_member(self, comp, scc_id):
        """每个成员从自身出发带路径剪枝探索;顶层探索前预算整体重置
        (估值只由自身探索决定),预算耗尽取一步有界回退值,均不跨
        上下文复用(复用会造成帧重复累加)。"""
        i = scc_id[comp[0]]
        explore_budget = 32
        sorted_adj = {
            a: sorted(self.functions[a].callee_addrs |
                      self.functions[a].tail_callee_addrs)
            for a in comp
        }

        def fallback(m):
            f = self.functions[m]
            best = 0
            tail_best = 0
            for w in sorted_adj[m]:
                if scc_id.get(w) != i:
                    v = self.functions[w].max_size
                else:
                    v = self.rec_depth * self.functions[w].frame_size
                if w in f.tail_callee_addrs and w not in f.callee_addrs:
                    if v > tail_best:
                        tail_best = v
                elif v > best:
                    best = v
            return max(self.rec_depth * f.frame_size + best,
                       tail_best), ['*Rec']

        def explore(m, on_path):
            if budget[m] <= 0:
                return fallback(m)
            budget[m] -= 1
            f = self.functions[m]
            best_c, best_ch = 0, []
            tail_c, tail_ch = 0, None
            for w in sorted_adj[m]:
                if scc_id.get(w) != i:
                    cf = self.functions[w]
                    c, ch = cf.max_size, cf.chain
                elif w in on_path:
                    # 重入边:环只展开一遍,不贡献帧
                    c = 0
                    ch = ['*Rec']
                else:
                    c, ch = explore(w, on_path | {w})
                if w in f.tail_callee_addrs and w not in f.callee_addrs:
                    # 尾调用边:帧已释放,不叠加 m 的帧
                    if c > tail_c:
                        tail_c, tail_ch = c, ch
                else:
                    if c > best_c:
                        best_c, best_ch = c, ch
            body = self.rec_depth * f.frame_size + best_c
            return self._pick_tail_or_body(
                f.name + '*Rec', body,
                [f.name + '*Rec'] + best_ch, tail_c, tail_ch)

        for a in sorted(comp):
            budget = {x: explore_budget for x in comp}
            cost, chain = explore(a, {a})
            f = self.functions[a]
            f.max_size = cost
            f.chain = chain
            f.depth = len(chain) - 1

    def _collect_miss_funcptr(self):
        # 地址被取用且未补全的函数(全局,不限入口可达),提示用户补 config
        for a in sorted(self.functions):
            f = self.functions[a]
            if f.is_ignore:
                continue
            if f.ptr_src_location and not f.manual_callees:
                for loc in f.ptr_src_location:
                    miss_key = (f.name, loc)
                    if miss_key not in self.miss_funcptr:
                        self.miss_funcptr.append(miss_key)

    def compute_all(self, entries):
        self.compute_values()
        self._collect_miss_funcptr()
        results = []
        for a in entries:
            if a not in self.functions:
                continue
            f = self.functions[a]
            results.append((a, f.max_size, f.chain, f.depth))
        return results

    def compute_interrupt_stack(self):
        self.compute_values()
        self._collect_miss_funcptr()

        entry_cost = 0
        for addr in self.interrupt_entries:
            if addr in self.functions:
                cost = self.functions[addr].max_size
                if cost > entry_cost:
                    entry_cost = cost

        isr_results = {}
        max_isr_cost = 0
        for irq_num, isr_addrs in sorted(self.interrupt_isrs.items()):
            irq_max = 0
            for addr in isr_addrs:
                if addr in self.functions:
                    cost = self.functions[addr].max_size
                    if cost > irq_max:
                        irq_max = cost
            isr_results[irq_num] = irq_max
            if irq_max > max_isr_cost:
                max_isr_cost = irq_max

        total = entry_cost + max_isr_cost
        return total, entry_cost, isr_results

    def print_recursive_warnings(self):
        if self.rec_cycles:
            print('[WARNING][Recursive function]:')
            for cycle in self.rec_cycles:
                print(cycle)

    def print_miss_funcptr_warnings(self):
        if self.miss_funcptr:
            for name, loc in self.miss_funcptr:
                print('[WARNING]Miss_funcptr:<{}>:{}'.format(
                    name, self._miss_site_suffix(loc)))

    def _miss_site_suffix(self, loc):
        # -m 1/2 用源码位置,无标记回退地址
        m = _SITE_ADDR_RE.search(loc)
        addr = int(m.group(1), 16) if m else 0
        if addr and self.mode >= 1:
            marker = self.line_map.get(addr)
            if marker:
                return '{} {}'.format(os.path.basename(marker[0]), marker[1])
        return '0x{:x}'.format(addr)

    def print_results_table(self, results):
        print('Entry           PeakStack  CallChain')
        for addr, cost, chain, depth in sorted(results, key=lambda x: -x[1]):
            func = self.functions[addr]
            mark = ''
            if func.is_rec:
                mark = '*Rec'
            chain_str = '>'.join(chain[:10]) if chain else ''
            if len(chain) > 10:
                chain_str += '...'
            print('{:<16} {:<10} {}{}'.format(
                func.name, '{}{}'.format(cost, mark), chain_str, ''))

    def print_call_tree(self, entries, filter_max=0, filter_local=0):
        for entry_addr in entries:
            if entry_addr not in self.functions:
                continue
            func = self.functions[entry_addr]
            cost = func.max_size

            print('function<{}>:0x{:x} stack cost={}'.format(func.name, func.addr, cost))
            print('symbol task_entry   task_stack_max_cost local_cost')

            visited = set()

            def _print_tree(addr, level, current_path, via_tail=False):
                # 路径上剪枝(环),visited 剪枝(菱形,防指数爆炸)
                if addr not in self.functions:
                    return
                if addr in current_path:
                    f = self.functions[addr]
                    if f.is_rec:
                        self._print_tree_rec_node(f, level, via_tail)
                    return
                if addr in visited and level > 0:
                    return
                visited.add(addr)

                f = self.functions[addr]
                if level > 0:
                    # -L/-l 只过滤子节点;根节点为用户指定的入口,必须显示
                    if filter_max > 0 and f.max_size < filter_max:
                        return
                    if filter_local > 0 and f.frame_size < filter_local:
                        return

                indent = '|     ' * level
                mark = self._tree_mark(f, via_tail)

                max_s = f.max_size if not f.is_ignore else 0
                local_s = f.frame_size if not f.is_ignore else 0

                print('{}symbol+--<{}>: {} {} {}'.format(
                    indent, f.name, max_s, local_s, mark).rstrip())

                if f.is_ignore:
                    return

                current_path.add(addr)
                # 更深的分支整体截断
                if level >= MAX_TREE_DEPTH:
                    self._print_tree_truncation(f, level)
                else:
                    for callee_addr in sorted(f.callee_addrs | f.tail_callee_addrs):
                        # 同时存在普通边与尾边时按普通边展示
                        via = callee_addr in f.tail_callee_addrs \
                            and callee_addr not in f.callee_addrs
                        _print_tree(callee_addr, level + 1, current_path, via)
                current_path.discard(addr)

                if level < MAX_TREE_DEPTH and f.ptr_src_location \
                        and not f.manual_callees:
                    self._print_tree_unresolved(f, level)

            _print_tree(entry_addr, 0, set())
            print()

    def _print_tree_rec_node(self, f, level, via_tail):
        indent = '|     ' * level
        mark = '*Rec'
        if via_tail:
            mark += '*Tail'
        print('{}symbol+--<{}>: {} {} {}'.format(
            indent, f.name, 0, 0, mark))

    def _tree_mark(self, f, via_tail):
        # *Rec / *Ignore(覆盖)/ *Tail(叠加)
        mark = ''
        if f.is_rec:
            mark = '*Rec'
        if f.is_ignore:
            mark = '*Ignore'
        if via_tail:
            mark += '*Tail'
        return mark

    def _print_tree_truncation(self, f, level):
        has_below = bool(f.callee_addrs | f.tail_callee_addrs) \
            or (f.ptr_src_location and not f.manual_callees)
        if has_below:
            print('{}symbol+--... (depth > {} truncated)'.format(
                '|     ' * (level + 1), MAX_TREE_DEPTH))

    def _print_tree_unresolved(self, f, level):
        indent2 = '|     ' * (level + 1)
        for loc in f.ptr_src_location:
            print('{}symbol+--[unresolved] {}: 0 0'.format(indent2, loc))

    def print_statistics(self, entries):
        total = len(self.functions)
        print('uncalled function count = {}; total num = {}'.format(len(entries), total))

    def output_json(self, json_path, irq_data=None):
        data = {}
        if irq_data is not None:
            # 中断汇总为顶层保留键 Interrupt(不以 0x 开头,与函数地址键可区分)
            data['Interrupt'] = irq_data
        for addr, func in self.functions.items():
            key = '0x{:x}'.format(addr)
            data[key] = {
                'FuncName': func.name,
                'FuncAddr': key,
                'LocalCost': func.frame_size,
                'MaxCost': func.max_size,
                'Called': ['0x{:x}'.format(c) for c in
                           sorted(func.callee_addrs | func.tail_callee_addrs)],
                'Location': func.location,
                'IsCalled': func.is_called,
                'IsRec': func.is_rec,
                'Depth': func.depth,
                'PtrVarName': func.ptr_var_name,
            }

        out_dir = os.path.dirname(json_path)
        if out_dir and not os.path.exists(out_dir):
            os.makedirs(out_dir, exist_ok=True)

        with open(json_path, 'w') as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        print('[INFO] JSON written to {}'.format(json_path), file=sys.stderr)

    def run(self, asm_file, single_point=False, entry_names=None,
            output_tree=False, output_json=False, json_path=None,
            filter_max=0, filter_local=0, irq_mode=False, mode=0):
        self.mode = mode
        self.parse_disasm(asm_file)
        self.load_config()

        if irq_mode:
            total, entry_cost, isr_results = self.compute_interrupt_stack()
            print('interrupt:stack cost = {}'.format(total))
            print('irq entry cost = {}'.format(entry_cost))
            for irq_num in sorted(isr_results.keys()):
                print('irq {} stack cost = {}'.format(irq_num, isr_results[irq_num]))
            self.update_config()
            if output_json and json_path:
                self.output_json(json_path, irq_data={
                    'TotalStackCost': total,
                    'EntryCost': entry_cost,
                    'IsrCosts': {str(n): isr_results[n]
                                 for n in sorted(isr_results)},
                })
            return

        entries = self.enumerate_entries(single_point, entry_names or [])
        results = self.compute_all(entries)

        # 全量模式结果表格与调用树仅 -out 时输出;单点模式默认输出
        self.print_recursive_warnings()
        if self.rec_cycles:
            print()
        self.print_miss_funcptr_warnings()
        if self.miss_funcptr:
            print()
        if single_point or output_tree:
            self.print_results_table(results)
            if results:
                print()
        if output_tree:
            self.print_call_tree(entries, filter_max, filter_local)
        if not single_point:
            self.print_statistics(entries)

        self.update_config()

        if output_json and json_path:
            self.output_json(json_path)


def detect_arch(asm_file):
    with open(asm_file, 'r', errors='replace') as f:
        for i, line in enumerate(f):
            if i > 50:
                break
            low = line.lower()
            if 'elf32-littlearm' in low or 'elf64-littleaarch64' in low:
                return 'arm'
            if 'elf32-littleriscv' in low or 'riscv' in low:
                return 'riscv'
    return 'arm'


def main():
    parser = argparse.ArgumentParser(
        description='Static stack usage analyzer for ARM/RISC-V disassembly.',
        formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument('--asm', required=True, help='反汇编文件路径')
    parser.add_argument('--arch', choices=['arm', 'riscv'],
                        help='架构(不指定则自动识别)')
    parser.add_argument('-t', '--entries', help='任务入口函数名(逗号分隔)')
    parser.add_argument('-sp', action='store_true', help='单点模式(配合 -t)')
    parser.add_argument('-r', '--rec-depth', type=int, default=1,
                        help='递归展开层数(默认 1,须 >= 1)')
    parser.add_argument('-out', action='store_true',
                        help='输出结果表格与调用树到终端'
                             '(全量模式默认只输出告警与统计)')
    parser.add_argument('-j', '--json', action='store_true', help='输出 JSON')
    parser.add_argument('-L', '--filter-max', type=int, default=0,
                        help='调用树过滤:max_size >= N 才显示')
    parser.add_argument('-l', '--filter-local', type=int, default=0,
                        help='调用树过滤:local_size >= N 才显示')
    parser.add_argument('-m', '--mode', type=int, default=0, choices=[0, 1, 2],
                        help='分析模式:0=不提取源码,1=objdump -S,2=objdump -l')
    parser.add_argument('-irq', action='store_true', help='中断栈分析模式')
    parser.add_argument('--config', default='./cfg/config.ini',
                        help='config.ini 路径')

    args = parser.parse_args()

    if args.irq and (args.entries or args.sp):
        print('[ERROR] -irq 与 -sp/-t 互斥，-irq 模式只统计中断栈',
              file=sys.stderr)
        sys.exit(1)

    # 深调用链与调用树递归打印需要更深的递归栈
    sys.setrecursionlimit(20000)

    arch = args.arch or detect_arch(args.asm)
    print('[INFO] Architecture: {}'.format(arch), file=sys.stderr)

    sys.path.insert(
        0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'src'))

    if args.rec_depth < 1:
        print('[ERROR] rec-depth must be >= 1', file=sys.stderr)
        sys.exit(1)
    analyzer = StackAnalyzer(arch, rec_depth=args.rec_depth)
    analyzer.config_path = args.config

    entry_names = []
    if args.entries:
        entry_names = [n.strip() for n in args.entries.split(',') if n.strip()]

    json_path = './out/funcstack.json' if args.json else None

    analyzer.run(
        asm_file=args.asm,
        single_point=args.sp,
        entry_names=entry_names,
        output_tree=args.out,
        output_json=args.json,
        json_path=json_path,
        filter_max=args.filter_max,
        filter_local=args.filter_local,
        irq_mode=args.irq,
        mode=args.mode,
    )


if __name__ == '__main__':
    main()
