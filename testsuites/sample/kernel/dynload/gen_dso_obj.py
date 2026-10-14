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
"""Convert the dynload test DSO binaries into ELF objects with objcopy."""

import argparse
import os
import struct
import subprocess
import sys


def mangle(basename):
    """Mirror the objcopy _binary_<...> symbol-name mangling."""
    return "".join(c if c.isalnum() or c == "_" else "_" for c in basename)


def patch_elf_flags(path, flags):
    """OR <flags> into the ELF32 header e_flags (offset 36); needed for the RISC-V float ABI."""
    with open(path, "r+b") as f:
        ident = f.read(16)
        if len(ident) != 16 or ident[:4] != b"\x7fELF":
            print("not an ELF object: %s" % path, file=sys.stderr)
            return False
        if ident[4] != 1:  # ELFCLASS32
            print("unsupported ELF class in %s" % path, file=sys.stderr)
            return False
        f.seek(36)  # ELF32 e_flags offset
        old = struct.unpack("<I", f.read(4))[0]
        f.seek(36)
        f.write(struct.pack("<I", old | flags))
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--objcopy", required=True,
                        help="cross objcopy command (may carry a ccache/xcache prefix)")
    parser.add_argument("--out-dir", required=True,
                        help="directory for the generated objects")
    parser.add_argument("--output-target", default="elf32-littlearm",
                        help="objcopy -O value, must match the target ELF class")
    parser.add_argument("--binary-architecture", default="arm",
                        help="objcopy -B value for the binary input files")
    parser.add_argument("--elf-flags", type=lambda x: int(x, 0), default=0,
                        help="value OR-ed into the generated ELF32 e_flags "
                             "(e.g. 0x4 = EF_RISCV_FLOAT_ABI_DOUBLE; 0 keeps "
                             "objcopy's default)")
    parser.add_argument("inputs", nargs="+",
                        help="DSO files, relative to the build output directory")
    args = parser.parse_args()

    # Drop a meaningless compile-cache prefix (ccache/xcache) from the objcopy command.
    objcopy = args.objcopy.split()
    if objcopy and os.path.basename(objcopy[0]) in ("ccache", "xcache"):
        objcopy = objcopy[1:]
    if not objcopy:
        print("invalid --objcopy command: %r" % args.objcopy, file=sys.stderr)
        return 1

    if not os.path.isdir(args.out_dir):
        os.makedirs(args.out_dir, exist_ok=True)

    for src in args.inputs:
        if not os.path.isfile(src):
            print("input not found: %s" % src, file=sys.stderr)
            return 1
        base = os.path.basename(src)
        # The child runs in the source directory; pass absolute output paths.
        dst = os.path.abspath(os.path.join(args.out_dir, base + ".o"))
        cmd = objcopy + [
            "-I", "binary",
            "-O", args.output_target,
            "-B", args.binary_architecture,
            # Move the constant image bytes from .data into a read-only .rodata section.
            "--rename-section",
            ".data=.rodata.dso.%s,alloc,load,readonly,data,contents" % mangle(base),
            base,
            dst,
        ]
        # Run in the file's directory with the bare name to keep _binary_* symbols stable.
        result = subprocess.run(cmd, cwd=os.path.dirname(src) or ".")
        if result.returncode != 0:
            print("objcopy failed for %s" % src, file=sys.stderr)
            return 1
        if args.elf_flags and not patch_elf_flags(dst, args.elf_flags):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
