#!/usr/bin/env python3
# Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
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

r"""Export liteos_m kernel source-file list (from GN build graph) as a CMake
fragment, so downstream projects can compile liteos_m sources directly with
their own toolchain — no GN/Ninja dependency at build time.

Usage (maintainer, run once per liteos_m upgrade / config change):
  1. gn gen <out> --args='device_path="//path/to/board/liteos_m" \
                   product_path="//path/to/board" \
                   device_company="xxx" ohos_kernel_type="liteos_m"'
  2. python3 tools/export_cmake_sources.py --gn-out <out>
  3. commit the regenerated build/cmake/sources.cmake

Integrators never run this; they consume the committed build/cmake/sources.cmake.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys


# Repo copyright/license header (single-line 2020-2026 + 3-clause text),
# prepended by this script to menuconfig.snapshot.h — regeneration keeps it.
_LICENSE = (
    "Redistribution and use in source and binary forms, with or without modification,",
    "are permitted provided that the following conditions are met:",
    "",
    "1. Redistributions of source code must retain the above copyright notice, this list of",
    "   conditions and the following disclaimer.",
    "",
    "2. Redistributions in binary form must reproduce the above copyright notice, this list",
    "   of conditions and the following disclaimer in the documentation and/or other materials",
    "   provided with the distribution.",
    "",
    "3. Neither the name of the copyright holder nor the names of its contributors may be used",
    "   to endorse or promote products derived from this software without specific prior written",
    "   permission.",
    "",
    "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS",
    "\"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,",
    "THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR",
    "PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR",
    "CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,",
    "EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,",
    "PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;",
    "OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,",
    "WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR",
    "OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF",
    "ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.",
)
_C_LICENSE = "\n".join(
    ["/*",
     " * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.",
     " *"]
    + [(" * " + line).rstrip() for line in _LICENSE]
    + [" */"]) + "\n\n"


def find_gn(gn_path):
    if gn_path:
        return gn_path
    gn = os.environ.get("GN_BIN") or "gn"
    try:
        subprocess.run([gn, "--version"], capture_output=True, check=True)
        return gn
    except (FileNotFoundError, subprocess.CalledProcessError):
        sys.exit("[ERROR] gn not found. Install GN or pass --gn / set GN_BIN.")


def gn_desc_json(gn, out_dir, label):
    """Run `gn desc --format=json`; return the parsed target dict.
    GN forwards BUILD.gn print() output ahead of the JSON — parse from '{'."""
    proc = subprocess.run(
        [gn, "desc", "--format=json", out_dir, label],
        capture_output=True, text=True, check=True)
    brace = proc.stdout.find("{")
    if brace < 0:
        raise ValueError(f"no JSON in gn desc output for {label}")
    data = json.loads(proc.stdout[brace:])
    # Top level is {label: {target-info}} — unwrap the single entry.
    if isinstance(data, dict) and len(data) == 1:
        return next(iter(data.values()))
    return data


def collect_labels(gn, out_dir):
    proc = subprocess.run(
        [gn, "desc", out_dir, "//kernel/liteos_m:libkernel", "deps", "--tree"],
        capture_output=True, text=True, check=True)
    labels = set()
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line.startswith("//"):
            continue
        # strip trailing toolchain suffix like "(//build/lite/toolchain:...)"
        label = re.sub(r"\([^)]*\)$", "", line).strip()
        if label.startswith("//build/") or label.endswith(":prebuilts"):
            continue
        labels.add(label)
    return sorted(labels)


def label_source_dir(label, gn_out_dir):
    """Map a GN label to its BUILD.gn dir: GN source root recovered from
    build.ninja's `gn --root=...` (build-dir relative), else __file__."""
    label_path = label.split(":")[0][2:]  # //a/b:c -> a/b
    root = None
    ninja = os.path.join(gn_out_dir, "build.ninja")
    if os.path.exists(ninja):
        with open(ninja) as f:
            for ln in f:
                m = re.search(r"--root=(\S+)", ln)
                if m:
                    root = os.path.normpath(
                        os.path.join(os.path.abspath(gn_out_dir), m.group(1)))
                    break
    if root is None or not os.path.isdir(root):
        root = os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.dirname(os.path.abspath(__file__)))))
    return os.path.join(root, label_path)


def parse_module_switch(build_gn_dir):
    bg = os.path.join(build_gn_dir, "BUILD.gn")
    if not os.path.exists(bg):
        return None
    with open(bg, errors="replace") as f:
        text = f.read()
    m = re.search(r"module_switch\s*=\s*defined\(\s*(LOSCFG_\w+)\s*\)", text)
    return m.group(1) if m else None


def parse_config_h(gn_out_dir):
    header = os.path.join(gn_out_dir, "config.h")
    values = {}
    if not os.path.exists(header):
        return values, None
    with open(header, errors="replace") as f:
        for ln in f:
            m = re.match(r'^#define\s+(LOSCFG_\w+)\s+(\S+)\s*$', ln)
            if m:
                values[m.group(1)] = m.group(2)
    return values, header


def h_to_cmake_value(raw):
    if raw.startswith('"') and raw.endswith('"'):
        return raw[1:-1]
    return raw


def write_kconfig_values(path, values, config_used, header_src):
    with open(path, "w") as f:
        f.write(f"#{'=' * 77}\n")
        f.write("# AUTO-GENERATED by tools/export_cmake_sources.py — DO NOT EDIT.\n")
        f.write("# Snapshot of the GN genconfig output (config.h) for board config:\n")
        f.write(f"#   {config_used}\n")
        f.write(f"# Source header: {header_src}\n")
        f.write("# Contains the *expanded* Kconfig state, including symbols derived\n")
        f.write("# via `select` — which plain text parsing of the .config cannot see.\n")
        f.write(f"# {len(values)} LOSCFG_* symbols.\n")
        f.write(f"#{'=' * 77}\n\n")
        f.write("set(EVOLVE_KCONFIG_SNAPSHOT_LOADED ON)\n")
        f.write(f'set(EVOLVE_KCONFIG_SNAPSHOT_HEADER "{header_src}")\n\n')
        for name, raw in values.items():
            v = h_to_cmake_value(raw)
            f.write(f'set({name} "{v}")\n')


def find_board_config(gn_out_dir):
    args_gn = os.path.join(gn_out_dir, "args.gn")
    if not os.path.exists(args_gn):
        return "(unknown)"
    with open(args_gn) as f:
        for ln in f:
            m = re.search(r'product_path\s*=\s*"([^"]+)"', ln)
            if m:
                return f"{m.group(1)}/kernel_configs/<type>.config"
    return "(unknown)"


def write_kconfig_outputs(gn_out_dir, out_dir, config_used):
    kconfig_values, config_header = parse_config_h(gn_out_dir)
    if not kconfig_values or not config_header:
        return
    values_path = os.path.join(out_dir, "kconfig_values.cmake")
    write_kconfig_values(values_path, kconfig_values, config_used, config_header)
    print(f"[OK] wrote {values_path} ({len(kconfig_values)} symbols)")
    # The genconfig header becomes the compile-time menuconfig.h verbatim.
    snap_path = os.path.join(out_dir, "menuconfig.snapshot.h")
    with open(snap_path, "w") as out, open(config_header) as src:
        out.write(_C_LICENSE)
        shutil.copyfileobj(src, out)
    print(f"[OK] wrote {snap_path}")


def cmake_rel_files(sources, seen_files):
    """Map //root-relative sources to (cmake_expr, rel) pairs, deduped.
    Exprs are generated-fragment TEXT (CMake vars, forward slashes) —
    f-strings, never os.path.join."""
    files = []
    for s in sources:
        if not s.startswith("//"):
            continue  # absolute/toolchain-internal paths
        rel = s[2:]
        if rel in seen_files:
            continue  # shared source compiled by several targets — keep once
        seen_files.add(rel)
        if rel.startswith("kernel/liteos_m/"):  # this repo -> its top dir
            expr = f"${{EVOLVE_TOP_DIR}}/{rel[len('kernel/liteos_m/'):]}"
        else:  # third_party/board -> GN source root
            expr = f"${{EVOLVE_SOURCE_ROOT}}/{rel}"
        files.append((expr, rel))
    return files


def collect_groups(gn, gn_out_dir, labels):
    groups = []
    seen_files = set()
    for label in labels:
        try:
            info = gn_desc_json(gn, gn_out_dir, label)
        except (subprocess.CalledProcessError, ValueError):
            continue  # targets gn desc cannot show — skip
        ttype = info.get("type", "")
        if ttype not in ("source_set", "static_library", "executable"):
            continue  # groups / configs carry no sources
        sources = info.get("sources", [])
        if not sources:
            continue
        src_dir = label_source_dir(label, gn_out_dir)
        switch = parse_module_switch(src_dir)
        name = label[2:].replace("/", "_").replace(":", "_")
        files = cmake_rel_files(sources, seen_files)
        if files:
            groups.append((name, switch, sorted(files)))
    return groups, seen_files


def write_sources(path, groups, seen_files):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    banner = f"""\
#{'=' * 77}
# AUTO-GENERATED by tools/export_cmake_sources.py — DO NOT EDIT.
# Regenerate after upgrading liteos_m or changing a board config:
#   gn gen <out> --args='...'
#   python3 tools/export_cmake_sources.py --gn-out <out>
#
# Generated from GN dep tree of //kernel/liteos_m:libkernel
# Modules: {len(groups)}   Source files: {len(seen_files)}
#
# Variables expected to be set by the includer:
#   EVOLVE_TOP_DIR      — liteos_m source root (this repo)
#   EVOLVE_SOURCE_ROOT  — GN source root hosting third_party/
#                          and the board dir (defaults to the
#                          parent of EVOLVE_TOP_DIR)
#   LOSCFG_* switches   — from the generated menuconfig config
#
# Each group is guarded by the LOSCFG_* switch parsed from the
# module's BUILD.gn (module_switch = defined(LOSCFG_...)).

"""
    with open(path, "w") as f:
        f.write(banner)
        for name, switch, files in groups:
            f.write(f"# ---- module: {name}")
            f.write(f"  (switch: {switch})\n" if switch else "  (always)\n")
            if switch:
                f.write(f"if({switch})\n")
            f.write("  list(APPEND EVOLVE_KERNEL_SOURCES\n")
            for expr, _rel in files:
                f.write(f"    {expr}\n")
            f.write("  )\n")
            if switch:
                f.write("endif()\n")
            f.write("\n")


def print_summary(path, groups):
    total = sum(len(g[2]) for g in groups)
    print(f"[OK] wrote {path}")
    print(f"     {len(groups)} modules, {total} source files")
    for name, switch, files in groups:
        print(f"       {name:55s} {len(files):3d} files  {switch or '-'}")


def main():
    ap = argparse.ArgumentParser(
        description="Export liteos_m GN source list as CMake fragment")
    ap.add_argument("--gn-out", required=True,
                    help="GN output dir (already `gn gen`-ed)")
    ap.add_argument("--output",
                    default=os.path.join(os.path.dirname(os.path.dirname(
                        os.path.abspath(__file__))),
                        "build", "cmake", "sources.cmake"),
                    help="output .cmake path (default: build/cmake/sources.cmake)")
    ap.add_argument("--gn", default=None, help="path to gn binary")
    args = ap.parse_args()

    gn = find_gn(args.gn)
    if not os.path.isdir(args.gn_out):
        sys.exit(f"[ERROR] gn out dir not found: {args.gn_out}")

    labels = collect_labels(gn, args.gn_out)
    print(f"[INFO] collected {len(labels)} candidate labels from dep tree")

    config_used = find_board_config(args.gn_out)
    write_kconfig_outputs(args.gn_out, os.path.dirname(args.output), config_used)

    groups, seen_files = collect_groups(gn, args.gn_out, labels)
    write_sources(args.output, groups, seen_files)
    print_summary(args.output, groups)


if __name__ == "__main__":
    main()
