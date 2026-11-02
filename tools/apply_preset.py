#!/usr/bin/env python3
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
from kconfiglib import Kconfig


def find_preset_managed_syms(kconf):
    """Find symbols whose defaults have a KERNEL_RAM_PRESET_* condition.

    The condition is matched via str(cond) to catch both simple Symbol
    conditions and compound AND/OR expressions.
    """
    managed = set()
    for sym in kconf.unique_defined_syms:
        for default_val, cond in sym.defaults:
            if "KERNEL_RAM_PRESET" in str(cond):
                managed.add(sym.name)
                break
    return managed


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

    kconf.syms["KERNEL_RAM_PRESET"].set_value(2)
    kconf.syms[preset_syms[0]].set_value(2)

    kconf.write_config()


if __name__ == "__main__":
    main()
