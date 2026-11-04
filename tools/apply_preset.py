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
