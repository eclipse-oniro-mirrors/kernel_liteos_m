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
"""Build the dynload test DSO/REL images with the cross compiler."""

import argparse
import os
import shlex
import subprocess
import sys

# Shared-object recipes: output -> (inputs, extra flags).
SO_RULES = [
    ("basic.so", ["dynld_basic.c"], []),
    ("ossym.so", ["dynld_ossym.c"], []),
    # The SONAME is added for RISC-V only, inside main().
    ("dep_b.so", ["dynld_dep_b.c"], []),
    # Input order matters for lld (.dynsym order); dep_b.so must go first.
    ("dep_a.so", ["dep_b.so", "dynld_dep_a.c"], []),
]

# Relocatable-object recipes: output -> source file.
OBJ_RULES = [
    ("objbasic.o", "dynld_objbasic.c"),
    ("objossym.o", "dynld_objossym.c"),
    ("objundef.o", "dynld_objundef.c"),
    ("objmovwt.o", "dynld_objmovwt.c"),
]

SO_FLAGS = [
    "-fPIC",
    "-shared",
    "-nostdlib",
    "-nostartfiles",
    "-z",
    "max-page-size=4",
]

# exec.so: built without -shared, yields an ET_EXEC reject case.
EXEC_FLAGS = [
    "-nostdlib",
    "-nostartfiles",
    "-z",
    "max-page-size=4",
]

OBJ_FLAGS = [
    "-c",
    "-O2",
    "-nostdlib",
]


def is_clang(cc_cmd):
    """Tell clang-family drivers (BiSheng/llvm) apart from gcc by name."""
    return "clang" in os.path.basename(cc_cmd[-1])


def run(cc_cmd, flags, cwd, what):
    """Run the compiler in <cwd>; print its output on failure."""
    cmd = cc_cmd + flags
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        print("failed to build %s: %s" % (what, " ".join(cmd)),
              file=sys.stderr)
        return False
    return True


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cc", required=True,
                        help="cross compiler command (may carry a ccache/xcache "
                             "prefix and an absolute toolchain path)")
    parser.add_argument("--arch", required=True,
                        choices=["arm", "arm64", "riscv"],
                        help="target architecture; selects the output "
                             "subdirectory and the arch-specific tweaks")
    parser.add_argument("--src-dir", required=True,
                        help="directory holding the dynld_*.c sources")
    parser.add_argument("--out-dir", required=True,
                        help="directory for the generated images "
                             "(.../dso/<arch>)")
    parser.add_argument("--arch-flags", default="",
                        help="arch flags for the compiler (e.g. "
                             "\"-mcpu=cortex-m4 -mthumb\" or "
                             "\"--target=riscv32 -march=rv32imfdc "
                             "-mabi=ilp32d\"), passed after the fixed "
                             "recipe flags")
    args = parser.parse_args()

    cc_cmd = shlex.split(args.cc)
    if not cc_cmd:
        print("invalid --cc command: %r" % args.cc, file=sys.stderr)
        return 1

    src_dir = os.path.abspath(args.src_dir)
    out_dir = os.path.abspath(args.out_dir)
    so_outputs = {output for output, _, _ in SO_RULES}
    for src in [s for _, inputs, _ in SO_RULES for s in inputs if s not in so_outputs] + \
               [s for _, s in OBJ_RULES]:
        if not os.path.isfile(os.path.join(src_dir, src)):
            print("source not found: %s" % os.path.join(src_dir, src),
                  file=sys.stderr)
            return 1
    os.makedirs(out_dir, exist_ok=True)

    clang = is_clang(cc_cmd)
    arch_flags = shlex.split(args.arch_flags)

    # -mslow-flash-data is an arm-none-eabi-gcc-only option.
    movwt_extra = ["-mslow-flash-data"] if args.arch == "arm" and not clang else []

    for output, inputs, extra in SO_RULES:
        # Rule outputs (dep_b.so) come from out-dir by bare name so DT_NEEDED stays "dep_b.so".
        cc_inputs = [s if s in so_outputs else os.path.join(src_dir, s)
                     for s in inputs]
        flags = SO_FLAGS + extra + arch_flags
        if args.arch == "riscv" and output == "dep_b.so":
            # lld records the input path in DT_NEEDED without a SONAME; only RISC-V needs it.
            flags += ["-Wl,-soname,dep_b.so"]
        if not run(cc_cmd, flags + cc_inputs + ["-o", output], out_dir, output):
            return 1

    if not run(cc_cmd, EXEC_FLAGS + arch_flags +
               [os.path.join(src_dir, "dynld_basic.c"), "-o", "exec.so"],
               out_dir, "exec.so"):
        return 1

    for output, src in OBJ_RULES:
        extra = movwt_extra if output == "objmovwt.o" else []
        if not run(cc_cmd, OBJ_FLAGS + extra + arch_flags +
                   [os.path.join(src_dir, src), "-o", output],
                   out_dir, output):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
