#!/usr/bin/env python3
"""LiteOS-M allnoconfig with platform preservation.

Two-step approach adapted from Huawei_LiteOS usr_config.py:
  1. set_value(2 if is_allnoconfig_y else 0)  — keep only choice defaults + select chain
  2. load_allconfig(seed)                      — restore platform choice member from seed

The seed is derived from the vendor config: only LOSCFG_PLATFORM_* symbols are
extracted, so functional options (SHELL/FS/BACKTRACE/...) are NOT restored and
remain closed.  The user is expected to re-enable them via menuconfig.

Environment (set by Makefile):
  CONFIG_=LOSCFG_  srctree=<kernel/liteos_m>  BOARD_COMPANY=<co>
  KCONFIG_CONFIG=.config  KCONFIG_ALLCONFIG=<vendor debug.config>
"""

from __future__ import absolute_import
import os
import re
import sys
import tempfile
from kconfiglib import Kconfig


def extract_platform_seed(vendor_config):
    prefix = os.environ.get("CONFIG_", "CONFIG_")
    pattern = re.compile(r"^" + re.escape(prefix) + r"PLATFORM_[A-Z_0-9]+=y\b")

    seed_lines = []
    with open(vendor_config, "r") as f:
        for line in f:
            if pattern.match(line):
                seed_lines.append(line)

    if not seed_lines:
        sys.exit("allnoconfig.py: no LOSCFG_PLATFORM_*=y found in %s" % vendor_config)

    fd, seed_path = tempfile.mkstemp(prefix="liteos_seed_", suffix=".config")
    with os.fdopen(fd, "w") as f:
        f.writelines(seed_lines)
    return seed_path


def main():
    vendor_config = os.environ.get("KCONFIG_ALLCONFIG")
    if not vendor_config:
        sys.exit("allnoconfig.py: KCONFIG_ALLCONFIG must point to vendor debug.config")

    seed_path = extract_platform_seed(vendor_config)
    try:
        os.environ["KCONFIG_ALLCONFIG"] = seed_path

        kconf = Kconfig(filename="Kconfig")

        kconf.warn = False
        for sym in kconf.unique_defined_syms:
            sym.set_value(2 if sym.is_allnoconfig_y else 0)
        kconf.warn = True

        kconf.load_allconfig("allno.config")

        print(kconf.write_config())
    finally:
        os.unlink(seed_path)


if __name__ == "__main__":
    main()
