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
"""Inputs and shared base: repo root, diagnostics, parsing and evaluation."""

import operator
import re
import sys
from collections import OrderedDict
from pathlib import Path

# src/input.py -> parents[3] is the liteos_m repo root
REPO_ROOT = Path(__file__).resolve().parents[3]


def die(msg):
    sys.stderr.write("footprint: error: %s\n" % msg)
    sys.exit(1)


def warn(msg):
    sys.stderr.write("footprint: warning: %s\n" % msg)


def align_up(v, a):
    return (v + a - 1) // a * a


def fmt_kb(nbytes):
    return None if nbytes is None else round(nbytes / 1024.0, 2)


def read_file(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return None


TOKEN_RE = re.compile(r"""
    \s*(?:
        (?P<hex>0[xX][0-9a-fA-F]+[uUlL]*)
      | (?P<dec>\d+[uUlL]*)
      | (?P<sizeof>sizeof\s*\(\s*[^()]+\s*\))
      | (?P<ident>[A-Za-z_]\w*)
      | (?P<op><<|>>|<=|>=|==|!=|&&|\|\||[-+*/%()<>!~])
    )""", re.VERBOSE)


def _token_of(m):
    """Convert one TOKEN_RE match into a (kind, value) token."""
    if m.group("hex"):
        return ("int", int(m.group("hex").rstrip("uUlL"), 16))
    if m.group("dec"):
        return ("int", int(m.group("dec").rstrip("uUlL")))
    if m.group("sizeof"):
        inner = m.group("sizeof")
        inner = inner[inner.index("(") + 1:inner.rindex(")")].strip()
        return ("sizeof", inner)
    if m.group("ident"):
        return ("ident", m.group("ident"))
    return ("op", m.group("op"))


def tokenize_expr(expr):
    toks = []
    pos = 0
    while pos < len(expr):
        m = TOKEN_RE.match(expr, pos)
        if not m or m.end() == pos:
            if expr[pos:].strip() == "":
                break
            raise ValueError("bad token at %r" % expr[pos:pos + 20])
        pos = m.end()
        toks.append(_token_of(m))
    return toks


# comparison operator tables for the precedence-climbing evaluator
_EQ_CMP = {"==": operator.eq, "!=": operator.ne}
_REL_CMP = {"<=": operator.le, ">=": operator.ge,
            "<": operator.lt, ">": operator.gt}


class ExprEval:
    """Precedence-climbing evaluator over the token list."""

    def __init__(self, value_resolver, sizeof_resolver):
        self.vres = value_resolver
        self.sres = sizeof_resolver

    def eval(self, expr):
        self.toks = tokenize_expr(expr)
        self.i = 0
        v = self._expr()
        if self.i != len(self.toks):
            raise ValueError("trailing tokens in %r" % expr)
        return v

    def _peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else (None, None)

    def _next(self):
        t = self._peek()
        self.i += 1
        return t

    def _accept(self, *ops):
        t = self._peek()
        if t[0] == "op" and t[1] in ops:
            self.i += 1
            return t[1]
        return None

    def _expr(self):
        v = self._logor()
        return v

    def _logor(self):
        v = self._logand()
        while self._accept("||"):
            v = 1 if (v or self._logand()) else 0
        return v

    def _logand(self):
        v = self._eq()
        while self._accept("&&"):
            v = 1 if (v and self._eq()) else 0
        return v

    def _eq(self):
        v = self._rel()
        while True:
            op = self._accept("==", "!=")
            if op is None:
                return v
            v = 1 if _EQ_CMP[op](v, self._rel()) else 0

    def _rel(self):
        v = self._shift()
        while True:
            op = self._accept("<=", ">=", "<", ">")
            if op is None:
                return v
            v = 1 if _REL_CMP[op](v, self._shift()) else 0

    def _shift(self):
        v = self._add()
        while True:
            if self._accept("<<"):
                v = v << self._add()
            elif self._accept(">>"):
                v = v >> self._add()
            else:
                return v

    def _add(self):
        v = self._mul()
        while True:
            if self._accept("+"):
                v = v + self._mul()
            elif self._accept("-"):
                v = v - self._mul()
            else:
                return v

    def _mul(self):
        v = self._unary()
        while True:
            op = self._accept("*", "/", "%")
            if op is None:
                return v
            r = self._unary()
            if op == "*":
                v = v * r
            elif op == "/":
                v = v // r if r else 0
            else:
                v = v % r if r else 0

    def _unary(self):
        if self._accept("-"):
            return -self._unary()
        if self._accept("+"):
            return self._unary()
        if self._accept("!"):
            return 0 if self._unary() else 1
        if self._accept("~"):
            return ~self._unary()
        return self._primary()

    def _primary(self):
        t = self._next()
        if t[0] == "int":
            return t[1]
        if t[0] == "sizeof":
            return self.sres(t[1])
        if t[0] == "ident":
            return self.vres(t[1])
        if t == ("op", "("):
            v = self._expr()
            if not self._accept(")"):
                raise ValueError("missing )")
            return v
        raise ValueError("unexpected token %r" % (t,))


class MapRec:
    __slots__ = ("isec", "osec", "size", "attr", "symbol")

    def __init__(self, isec, osec, size, attr):
        self.isec = isec
        self.osec = osec
        self.size = size
        self.attr = attr
        self.symbol = ""


OUTSEC_HDR = re.compile(r"^(\.[A-Za-z_][\w.\-]*)\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)")
OBJLINE = re.compile(r"^\s*([^\s]+\.(?:o|a))(?:\(([^)]+)\))?\s*$")
ISEC_HDR = re.compile(r"^ (\S+)$")
ISEC_INLINE = re.compile(r"^ (\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s*(.*)$")
ADDR_SIZE = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s*(.*)$")
SYMBOL_ONLY = re.compile(r"^\s+0x[0-9a-fA-F]+\s+([A-Za-z_.$][\w.$]*)\s*$")
FILL_LINE = re.compile(r"^\s*\*fill\*\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)")
# non-allocated sections: map records there share/echo addresses freely
# (no relocation), and they are classified "ignore" anyway
NONALLOC_OSEC = re.compile(
    r"^\.(?:debug|stab|comment|note|ARM\.attributes|riscv\.attributes|gnu)")


class _MapState:
    """Mutable per-map parsing state shared by the _map_* line helpers."""

    __slots__ = ("recs", "outsec_sizes", "outsec_fills", "outsec_relax",
                 "covered", "cur_osec", "cur_isec", "cur_obj", "discarded")

    def __init__(self):
        self.recs = []
        self.outsec_sizes = OrderedDict()
        self.outsec_fills = OrderedDict()
        self.outsec_relax = OrderedDict()
        self.covered = {}   # osec -> end of attributed span (merge/relax dedup)
        self.cur_osec = None
        self.cur_isec = None
        self.cur_obj = ""
        self.discarded = False

    def eff_size(self, osec, addr, size):
        """Effective attributed size for loadable sections.

        SHF_MERGE string dedup and relaxation make several input sections
        share one address range (identical strings, 'size before
        relaxing'); only the part not yet covered adds real bytes.
        """
        if osec is None or NONALLOC_OSEC.match(osec):
            return size
        prev = self.covered.get(osec, 0)
        end = addr + size
        eff = size if addr >= prev else max(0, end - prev)
        if end > prev:
            self.covered[osec] = end
        if eff < size:
            self.outsec_relax[osec] = self.outsec_relax.get(osec, 0) + (size - eff)
        return eff


def _map_outsec_header(st, m):
    st.cur_osec = m.group(1)
    # same-name output-section headers may appear multiple times
    # (orphan/multi-region placement) -> accumulate, don't lose
    st.outsec_sizes[st.cur_osec] = (st.outsec_sizes.get(st.cur_osec, 0) +
                                    int(m.group(2), 16))
    st.cur_isec = None
    st.cur_obj = ""


def _map_fill(st, line):
    fm = FILL_LINE.match(line)
    if fm and st.cur_osec and not st.discarded:
        st.outsec_fills[st.cur_osec] = (st.outsec_fills.get(st.cur_osec, 0) +
                                        int(fm.group(1), 16))


def _map_isec_inline(st, m):
    attr = m.group(4).strip()
    if attr:
        base = attr.split(" (")[0].strip()
    else:
        base = st.cur_obj
    st.recs.append(MapRec(m.group(1), st.cur_osec,
                          st.eff_size(st.cur_osec, int(m.group(2), 16),
                                      int(m.group(3), 16)), base))
    st.cur_isec = None


def _map_addr_size(st, m):
    addr = int(m.group(1), 16)
    size = int(m.group(2), 16)
    attr = m.group(3).strip()
    if attr and not attr.startswith("("):
        base = attr.split(" (")[0].strip()
    else:
        base = st.cur_obj
    if base and (".o" in base or ".a" in base):
        st.recs.append(MapRec(st.cur_isec, st.cur_osec,
                              st.eff_size(st.cur_osec, addr, size), base))
    elif attr and not base:
        pass
    else:
        # attr is a plain symbol name (COMMON-like line)
        st.recs.append(MapRec(st.cur_isec, st.cur_osec,
                              st.eff_size(st.cur_osec, addr, size), st.cur_obj))
    st.cur_isec = None


def _map_symbol_only(st, m):
    if st.recs and not st.recs[-1].symbol:
        st.recs[-1].symbol = m.group(1)
        st.cur_isec = None


# map lines that never contribute records or state
_MAP_SKIP_PREFIXES = ("LOAD ", "START GROUP", "END GROUP",
                      "Memory Configuration", "Linker script",
                      "Supported emulations:", "ARCH(")


def _map_record_line(st, line):
    """Record-producing map body lines (object/isec/symbol lines)."""
    m = OBJLINE.match(line)
    if m and not ADDR_SIZE.match(line):
        st.cur_obj = line.strip()
        st.cur_isec = None
        return None
    m = ISEC_INLINE.match(line)
    if m and m.group(1).startswith(".") and not st.discarded:
        _map_isec_inline(st, m)
        return None
    m = ISEC_HDR.match(line)
    if m and m.group(1).startswith("."):
        st.cur_isec = m.group(1)
        return None
    m = ADDR_SIZE.match(line)
    if m and st.cur_isec is not None and not st.discarded:
        _map_addr_size(st, m)
        return None
    m = SYMBOL_ONLY.match(line)
    if m and st.recs and not st.discarded:
        _map_symbol_only(st, m)
    return None


def _map_line(st, line):
    """Feed one map body line into `st`.

    Returns True only for the OUTPUT(...) line that ends the map body
    (what follows is the orphan listing of merged non-alloc sections
    (.debug_*, .riscv.attributes) with no output-section context).
    """
    if line.startswith("Discarded input sections"):
        st.discarded = True
        st.cur_isec = None
        return None
    if st.discarded and OUTSEC_HDR.match(line):
        st.discarded = False  # next real output section ends the block
    if line.startswith("OUTPUT("):
        return True
    if line.startswith(_MAP_SKIP_PREFIXES):
        return None
    m = OUTSEC_HDR.match(line)
    if m and not line.startswith(" "):
        _map_outsec_header(st, m)
        return None
    if line.lstrip().startswith("*fill*"):
        _map_fill(st, line)
        return None
    if line.lstrip().startswith("="):
        return None
    return _map_record_line(st, line)


def parse_map(path):
    st = _MapState()
    try:
        f = open(path, "r", encoding="utf-8", errors="replace")
    except OSError as e:
        die("cannot read map %s: %s" % (path, e))
    with f:
        for raw in f:
            if _map_line(st, raw.rstrip("\n")):
                break
    return st.recs, st.outsec_sizes, st.outsec_fills, st.outsec_relax


def rec_name(r):
    """Best-effort symbol name for a map record (symbol line or isec)."""
    if r.symbol:
        return r.symbol
    if r.isec.count(".") >= 1 and not re.fullmatch(r"\.\w+", r.isec):
        return r.isec.split(".", 1)[1]
    return r.isec


def _pp_evaluator(env):
    def ev_resolver(name):
        if name in env:
            return env[name]
        raise ValueError(name)

    def sz_resolver(_t):
        raise ValueError("sizeof not supported in #if")

    return ExprEval(ev_resolver, sz_resolver)


def _pp_eval_cond(text, env, evaluator):
    # defined(X) / defined X -> 0/1 before normal evaluation
    def repl_def(m):
        return "1" if m.group(1) in env else "0"
    text = re.sub(r"defined\s*\(\s*(\w+)\s*\)", repl_def, text)
    text = re.sub(r"defined\s+(\w+)", repl_def, text)
    try:
        return bool(evaluator.eval(text))
    except Exception:
        return False


def _pp_elif(s, cond_stack, env, evaluator):
    if cond_stack:
        outer = len(cond_stack) < 2 or all(cond_stack[:-1])
        cond_stack[-1] = _pp_eval_cond(s.split("elif", 1)[1], env, evaluator) \
            if outer else False


def _pp_define(m, defines, env, evaluator):
    defines[m.group(1)] = m.group(3).strip()
    # make it available to later #if in this file
    try:
        env[m.group(1)] = evaluator.eval(m.group(3))
    except Exception:
        pass


def _pp_directive(s, defines, env, evaluator, cond_stack):
    """Handle one preprocessor directive in `s`; True if it is one."""
    m = re.match(r"#\s*(ifdef|ifndef)\s+(\w+)", s)
    if m:
        present = m.group(2) in env and env[m.group(2)] != 0
        val = present if m.group(1) == "ifdef" else not present
        cond_stack.append(val)
        return True
    m = re.match(r"#\s*if(.*)$", s)
    if m:
        cond_stack.append(_pp_eval_cond(m.group(1), env, evaluator))
        return True
    if re.match(r"#\s*elif", s):
        _pp_elif(s, cond_stack, env, evaluator)
        return True
    if re.match(r"#\s*else", s):
        if cond_stack:
            cond_stack[-1] = not cond_stack[-1]
        return True
    if re.match(r"#\s*endif", s):
        if cond_stack:
            cond_stack.pop()
        return True
    m = re.match(r"#\s*undef\s+(\w+)", s)
    if m:
        if all(cond_stack):
            defines.pop(m.group(1), None)
            env.pop(m.group(1), None)
        return True
    m = re.match(r"#\s*define\s+(\w+)(\([^)]*\))?[ \t]+(.+)$", s)
    if m and not m.group(2):
        # object-like macro (function-like `#define F(x)` is not a
        # consumed directive: it stays in the active lines);
        # a VALUE starting with '(' is a parenthesized expression
        if all(cond_stack):
            _pp_define(m, defines, env, evaluator)
        return True
    return False


def preprocess_lines(lines, macros):
    """Process #ifdef/#ifndef/#if/#else/#endif/#define/#undef over `lines`.

    Returns (active_lines, local_defines) where local_defines only contains
    object-like macros defined in *this* file under active conditionals.
    """
    active = []
    defines = OrderedDict()
    cond_stack = []
    env = dict(macros)  # evaluation env for #if expressions
    evaluator = _pp_evaluator(env)
    for raw in lines:
        if _pp_directive(raw.strip(), defines, env, evaluator, cond_stack):
            continue
        if all(cond_stack):
            active.append(raw)
    return active, defines


def attr_prefix(attr):
    base = attr_basename(attr)
    if not base:
        return ""
    return base.split(".")[0]


def attr_basename(attr):
    if not attr:
        return ""
    m = re.search(r"\(([^)]+)\)\s*$", attr)
    if m:
        return m.group(1).split("/")[-1]
    return attr.replace("\\", "/").split("/")[-1]


def _coerce_macro(val):
    """Kconfig .config / config.h value -> macro value
    (y/1 -> defined, integer, unquoted string)."""
    if val == "y" or val == "1":
        return 1
    if re.match(r"^-?\d+$", val):
        return int(val)
    return val.strip('"')


def _dc_entry(line, macros, notset):
    """Apply one .config line; returns 1 if it was a LOSCFG_ entry."""
    m = re.match(r"^(?:CONFIG_)?(LOSCFG_\w+)=(.*)$", line)
    if m:
        name, val = m.group(1), m.group(2).strip()
        if val == "n":
            notset[name] = 0
        else:
            macros[name] = _coerce_macro(val)
        return 1
    m = re.match(r"^#\s*(?:CONFIG_)?(LOSCFG_\w+) is not set$", line)
    if m:
        notset[m.group(1)] = 0
        return 1
    return 0


def _dc_stream(f, macros, notset):
    nlines = 0
    for raw in f:
        line = raw.strip()
        if not line or line.startswith("#!") or line.startswith("$ "):
            continue
        nlines += _dc_entry(line, macros, notset)
    return nlines


def parse_dotconfig(path):
    """Parse a Kconfig .config file.

    Returns (macros, notset):
      macros - dict of DEFINED macros (Kconfig semantics: only =y / =value
               entries are defined in C)
      notset - dict of explicitly disabled macros (value 0, report only)
    """
    macros = {}
    notset = {}
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            nlines = _dc_stream(f, macros, notset)
    except OSError as e:
        die("cannot read .config %s: %s" % (path, e))
    if nlines < 5:
        die("%s does not look like a Kconfig .config file "
            "(no LOSCFG_ entries found)" % path)
    return macros, notset


def _ch_stream(f, macros):
    nlines = 0
    for raw in f:
        m = re.match(r"^#define\s+(LOSCFG_\w+)\s+(.+?)\s*$", raw.strip())
        if not m:
            continue
        val = m.group(2).strip()
        if val.startswith("("):        # function-like macro
            continue
        nlines += 1
        macros[m.group(1)] = _coerce_macro(val)
    return nlines


def parse_config_h(path):
    """Parse a generated config.h (OHOS out/ keeps only this, not the
    Kconfig .config) directly into the same (macros, notset) shape as
    parse_dotconfig.

    Semantics mirror the .config path: '1' -> defined (y), integers ->
    int, quoted -> unquoted string; anything undefined stays absent
    (fail-closed). Function-like macros are skipped.
    """
    macros = {}
    notset = {}
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            nlines = _ch_stream(f, macros)
    except OSError as e:
        die("cannot read config.h %s: %s" % (path, e))
    if nlines < 5:
        die("%s does not look like a generated config.h "
            "(no LOSCFG_ #defines found)" % path)
    return macros, notset


# --------------------------------------------------------------------------
# Board target_config.h detection + merge.
# los_config.h includes "target_config.h" (resolved from the board include
# path), and boards add macros beyond the Kconfig .config (e.g.
# LOSCFG_BASE_CORE_SWTMR_IN_ISR). The map attributions reveal the board
# directory, so the tool can merge the board config into the macro env.
# --------------------------------------------------------------------------
BOARD_DIR_RE = [
    re.compile(r"(?:^|/)((?:device|vendor)/[\w\-.]+/[\w\-.]+/liteos_m/board)/"),
    re.compile(r"(?:^|/)(targets/[\w\-.]+/liteos_m/board)/"),
]


def _board_dir_candidates(attr, found):
    """Record existing board dirs (containing target_config.h) for `attr`."""
    norm = attr.replace("\\", "/")
    for rx in BOARD_DIR_RE:
        m = rx.search(norm)
        if not m:
            continue
        rel = m.group(1)
        for base in (REPO_ROOT, REPO_ROOT.parent.parent):
            cand = base / rel
            if (cand / "target_config.h").is_file():
                found[str(cand)] = True
                break


def detect_board_dirs(recs):
    """Detect board dirs (containing target_config.h) from map attributions."""
    found = OrderedDict()
    for r in recs:
        if not r.attr:
            continue
        _board_dir_candidates(r.attr, found)
    return list(found)


def eval_macro_value(expr, env):

    def vres(name):
        if name in env and isinstance(env[name], int):
            return env[name]
        raise ValueError(name)

    def sres(_t):
        raise ValueError("sizeof")

    return ExprEval(vres, sres).eval(expr)


def apply_board_config(board_dirs, macros):
    """Merge board target_config.h defines into the macro env.

    Follows real compilation semantics: the board header is included after
    the Kconfig macros, so plain (re)definitions override, while
    #ifndef-guarded ones defer to existing values.
    """
    env = dict(macros)
    merged = 0
    for d in board_dirs:
        text = read_file(Path(d) / "target_config.h")
        if text is None:
            continue
        _active, defines = preprocess_lines(text.splitlines(), env)
        for name, expr in defines.items():
            if not name.startswith("LOSCFG_"):
                continue
            try:
                env[name] = eval_macro_value(expr, env)
                merged += 1
            except (ValueError, ZeroDivisionError):
                pass
    return env, merged


# exclusion display reason, priority ordered, matched against the
# exclusion NAME (library / path component)
