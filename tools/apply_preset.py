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
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
# OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""Detect KERNEL_RAM_PRESET_* in .config and re-apply conditional defaults.

After menuconfig, if a RAM preset is selected, this script clears user_value
on symbols whose defaults depend on KERNEL_RAM_PRESET_* so that
`default ... if KERNEL_RAM_PRESET_*` conditions take effect.

Only preset-managed symbols are cleared — board-level selections (platform,
timer, interrupt controller) are preserved.

Environment:
  CONFIG_=LOSCFG_  srctree=<kernel/liteos_m>
  KCONFIG_CONFIG=.config
"""

from __future__ import absolute_import
import os
import sys
from kconfiglib import AND, Kconfig, NOT, OR


def cond_symbol_names(cond, names=None):
    """Recursively collect leaf Symbol names from a condition expression.

    str(Symbol) returns the symbol's full definition source text, which
    accidentally contains 'KERNEL_RAM_PRESET' for any symbol whose own
    definition mentions a preset (test-report defect #2: 15 symbols like
    FS_LITTLEFS/SHELL_EXCINFO_DUMP/CPUP_* were misdetected as managed).
    Walking the expression tree and comparing exact leaf names avoids this.
    """
    if names is None:
        names = set()
    if isinstance(cond, tuple):
        for part in cond[1:]:
            cond_symbol_names(part, names)
    elif hasattr(cond, "name") and cond.name:
        names.add(cond.name)
    return names


def find_preset_managed_syms(kconf):
    """Find symbols whose defaults have a KERNEL_RAM_PRESET_* condition."""
    managed = set()
    for sym in kconf.unique_defined_syms:
        for default_val, cond in sym.defaults:
            names = cond_symbol_names(cond)
            if any(n.startswith("KERNEL_RAM_PRESET_") for n in names):
                managed.add(sym.name)
                break
    return managed


def find_preset_managed_choices(kconf):
    """Find choices whose choice-level defaults have a KERNEL_RAM_PRESET_* condition.

    Choice-level conditional defaults (e.g. the memory algorithm choice) are
    attached to the Choice object, not to member symbols, so member symbols
    never appear in find_preset_managed_syms.  The trailing underscore in the
    prefix matters: the enclosing "if KERNEL_RAM_PRESET" of the preset choice
    leaks onto its default conditions and must not count as a tier condition.
    """
    managed = []
    for choice in kconf.choices:
        for _, cond in choice.defaults:
            names = cond_symbol_names(cond)
            if any(n.startswith("KERNEL_RAM_PRESET_") for n in names):
                managed.append(choice)
                break
    return managed


def eval_cond(cond):
    """Evaluate a Kconfig condition expression to its tristate value (0/1/2)."""
    if cond is None:
        return 2
    if isinstance(cond, tuple):
        if cond[0] == AND:
            return min(eval_cond(c) for c in cond[1:])
        if cond[0] == OR:
            return max(eval_cond(c) for c in cond[1:])
        if cond[0] == NOT:
            return 2 - eval_cond(cond[1])
    return cond.tri_value


def reselect_choice_default(choice):
    """Re-select the first choice default whose condition holds.

    unset_value() on members does not invalidate Choice.selection's cache in
    older kconfiglib releases, so clearing user selections has no visible
    effect.  set_value() on the default member goes through the normal change
    path instead.  Falls back to unsetting members when no default applies.
    """
    for def_sym, cond in choice.defaults:
        if eval_cond(cond) and def_sym.visibility:
            def_sym.set_value(2)
            return
    for sym in choice.syms:
        sym.unset_value()


def main():
    kconf = Kconfig(filename="Kconfig")
    kconf.load_config()

    preset_syms = []
    for name in ("KERNEL_RAM_PRESET_TINY", "KERNEL_RAM_PRESET_SMALL", "KERNEL_RAM_PRESET_STANDARD"):
        sym = kconf.syms.get(name)
        if sym and sym.tri_value == 2:
            preset_syms.append(name)

    if not preset_syms:
        return

    if len(preset_syms) > 1:
        sys.exit("apply_preset.py: multiple presets selected: %s" % ", ".join(preset_syms))

    managed = find_preset_managed_syms(kconf)
    for name in managed:
        sym = kconf.syms.get(name)
        if sym:
            sym.unset_value()

    for choice in find_preset_managed_choices(kconf):
        reselect_choice_default(choice)

    kconf.syms["KERNEL_RAM_PRESET"].set_value(2)
    kconf.syms[preset_syms[0]].set_value(2)

    kconf.write_config()


if __name__ == "__main__":
    main()
