#!/usr/bin/env python3
# Copyright (c) 2026 morpheus.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Generate the libmetal / libmica include sets for the liteos_m AMP build.

The vanilla upstream snapshots (the libmetal headers shipped in the SoC repo
//device/soc/rockchip/rk3568_m/include/libmetal, and the libmica subtree of
kernel/liteos_m/compat/mcs) keep their cmake @PROJECT_*@ placeholders and
only receive their public include prefixes (metal/, mica/platform/) at cmake
install time. This script performs that configure_file/install step at build
time (same scheme as the standalone liteos_m Makefile documented in
kernel/liteos_m/compat/mcs/README.md):

    <out>/metal/*.h                          <- device/soc/rockchip/rk3568_m/include/
                                                libmetal/*.h
                                                (@PROJECT_*@ substituted,
                                                #cmakedefine resolved)
    <out>/metal/compiler/<cc>/*.h            <- lib/compiler/... (no tokens)
    <out>/metal/processor/aarch64/*.h        <- lib/processor/... (no tokens)
    <out>/metal/system/liteos/*.h            <- device/soc/rockchip/rk3568_m/include/
                                                libmetal/system/liteos/
                                                (LiteOS system backend)
    <out>/mica/platform/*.h                  <- kernel/liteos_m/compat/mcs/mica/libmica/
                                                lib/*.h (@PROJECT_SYSTEM@)
    <out>/mica/platform/system/liteos/*.h    <- kernel/liteos_m/compat/mcs/mica/platform/
                                                (LiteOS platform backend)

Token values mirror upstream cmake for this target: system "liteos",
processor aarch64, machine liteos. HAVE_STDATOMIC_H/HAVE_FUTEX_H are left
undefined: libmetal then falls back to metal/compiler/gcc/atomic.h, which
works with clang on freestanding arm64.

All directory arguments must be absolute paths (BUILD.gn passes
rebase_path(..., "")), so the script is independent of the working
directory. With --list it just prints every input header (one absolute
path per line) so the GN action can declare them as inputs without
glob() (not available in this tree's gn binary).
"""

import argparse
import os
import re

METAL_SUBST = {
    "@PROJECT_VERSION_MAJOR@": "1",
    "@PROJECT_VERSION_MINOR@": "3",
    "@PROJECT_VERSION_PATCH@": "0",
    "@PROJECT_VERSION@": '"1.3.0"',
    "@PROJECT_SYSTEM@": "liteos",
    "@PROJECT_SYSTEM_UPPER@": "LITEOS",
    "@PROJECT_PROCESSOR@": "aarch64",
    "@PROJECT_PROCESSOR_UPPER@": "AARCH64",
    "@PROJECT_MACHINE@": "liteos",
    "@PROJECT_MACHINE_UPPER@": "LITEOS",
}

MICA_SUBST = {
    "@PROJECT_SYSTEM@": "liteos",
}

# HAVE_STDATOMIC_H: leave undefined -> gcc/clang builtin atomics path.
# HAVE_FUTEX_H: no futex on bare-metal.
CMKEDEFINE_UNDEF = ("HAVE_STDATOMIC_H", "HAVE_FUTEX_H")

CMKDEF_PATTERN = re.compile(r"^([ \t]*)#cmakedefine[ \t]+([A-Za-z0-9_]+)",
                            re.MULTILINE)


def substitute(text, subst):
    for token, value in subst.items():
        text = text.replace(token, value)
    return text


def resolve_cmakedefine(text):
    def repl(match):
        indent, name = match.group(1), match.group(2)
        if name in CMKEDEFINE_UNDEF:
            return "%s/* #undef %s */" % (indent, name)
        return "%s#define %s 1" % (indent, name)

    return CMKDEF_PATTERN.sub(repl, text)


def write_header(src_path, dst_path, subst, cmakedefine):
    with open(src_path, "r") as handle:
        text = handle.read()
    text = substitute(text, subst)
    if cmakedefine:
        text = resolve_cmakedefine(text)
    if os.path.isfile(dst_path):
        with open(dst_path, "r") as handle:
            if handle.read() == text:
                return  # avoid touching mtimes when nothing changed
    dst_dir = os.path.dirname(dst_path)
    if not os.path.isdir(dst_dir):
        os.makedirs(dst_dir)
    with open(dst_path, "w") as handle:
        handle.write(text)


def walk_headers(*src_dirs):
    for src in src_dirs:
        src = os.path.normpath(src)
        for root, _dirs, files in os.walk(src):
            for name in sorted(files):
                if name.endswith(".h"):
                    yield os.path.join(root, name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metal-lib", required=True, dest="metal_lib",
                        help="third_party/libmetal/lib (absolute)")
    parser.add_argument("--metal-backend", required=True,
                        dest="metal_backend",
                        help="mcs/openamp/libmetal/libmetal-system (absolute)")
    parser.add_argument("--mica-lib", required=True, dest="mica_lib",
                        help="mcs/mica/libmica/lib (absolute)")
    parser.add_argument("--mica-platform", required=True,
                        dest="mica_platform",
                        help="mcs/mica/platform (absolute)")
    parser.add_argument("--out", help="output include root (absolute)")
    parser.add_argument("--list", action="store_true",
                        help="print input headers, one per line, and exit")
    args = parser.parse_args()

    src_dirs = [args.metal_lib, args.metal_backend, args.mica_lib,
                args.mica_platform]
    for src in src_dirs:
        if not os.path.isdir(src):
            raise SystemExit("gen_mcs_includes: missing source dir %s" % src)

    if args.list:
        for path in walk_headers(args.metal_backend, args.mica_lib,
                                 args.mica_platform):
            print(path)
        for path in walk_headers(args.metal_lib):
            rel = os.path.relpath(path, os.path.normpath(args.metal_lib))
            if rel.split(os.sep)[0] != "system":
                print(path)
        return

    if not args.out:
        raise SystemExit("gen_mcs_includes: --out is required without --list")
    out = os.path.normpath(args.out)

    # libmetal core headers (token substitution, cmakedefine resolution):
    # top-level lib/*.h become metal/*.h; the compiler/ and processor/
    # subtrees referenced by lib/compiler.h, lib/cpu.h and lib/atomic.h keep
    # their layout (no placeholders inside). lib/system/** is NOT copied:
    # the active system is "liteos" (metal_backend below).
    for src_path in walk_headers(args.metal_lib):
        rel = os.path.relpath(src_path, os.path.normpath(args.metal_lib))
        if rel.split(os.sep)[0] == "system":
            continue
        write_header(src_path, os.path.join(out, "metal", rel), METAL_SUBST,
                     cmakedefine=(os.sep not in rel))

    # LiteOS system backend overlaid as metal/system/liteos/
    for src_path in walk_headers(args.metal_backend):
        rel = os.path.relpath(src_path,
                              os.path.normpath(args.metal_backend))
        write_header(src_path,
                     os.path.join(out, "metal", "system", "liteos", rel), {},
                     False)

    # libmica platform ABI headers (mica/platform/xxx.h)
    for src_path in walk_headers(args.mica_lib):
        rel = os.path.relpath(src_path, os.path.normpath(args.mica_lib))
        write_header(src_path, os.path.join(out, "mica", "platform", rel),
                     MICA_SUBST, False)

    # LiteOS platform backend overlaid as mica/platform/system/liteos/
    for src_path in walk_headers(args.mica_platform):
        rel = os.path.relpath(src_path,
                              os.path.normpath(args.mica_platform))
        write_header(src_path,
                     os.path.join(out, "mica", "platform", "system", "liteos",
                                  rel), {}, False)

    with open(os.path.join(out, ".generated"), "w") as handle:
        handle.write("")


if __name__ == "__main__":
    main()
