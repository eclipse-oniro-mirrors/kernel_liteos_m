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

"""LiteOS-M allnoconfig with platform preservation.

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
