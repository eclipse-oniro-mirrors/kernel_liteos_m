#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#===============================================================================
# @brief  Export liteos_m kernel source-file list (from GN build graph) as a
#         CMake fragment, so downstream projects can compile liteos_m sources
#         directly with their own toolchain — no GN/Ninja dependency at build
#         time.
#
# Usage (maintainer, run once per liteos_m upgrade / config change):
#   1. gn gen <out> --args='device_path="//path/to/board/liteos_m" \
#                    product_path="//path/to/board" \
#                    device_company="xxx" ohos_kernel_type="liteos_m"'
#   2. python3 tools/export_cmake_sources.py --gn-out <out>
#   3. commit the regenerated build/cmake/sources.cmake
#
# Downstream (community integrator) never runs this script; they just consume
# the committed build/cmake/sources.cmake via the top-level CMakeLists.txt.
#===============================================================================

import argparse
import json
import os
import re
import subprocess
import sys


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
    """Run `gn desc --format=json` and return the parsed target dict.

    NOTE: GN forwards BUILD.gn print() output and build-arg warnings to
    stdout ahead of the JSON, so start parsing at the first '{'."""
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
    """Walk the //kernel/liteos_m:libkernel dep tree and collect candidate
    module labels (kernel / third_party / board modules)."""
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
    """Map a GN label to its BUILD.gn directory on disk.

    Label paths are relative to the GN source root. Recover the root from
    the regeneration command inside build.ninja (``gn --root=...``); the
    value is relative to the build dir. Fall back to this script's
    location: <root>/kernel/liteos_m/tools/export_cmake_sources.py.
    """
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
        # <root>/kernel/liteos_m/tools/export_cmake_sources.py -> <root>
        root = os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.dirname(os.path.abspath(__file__)))))
    return os.path.join(root, label_path)


def parse_module_switch(build_gn_dir):
    """Extract the LOSCFG_* switch guarding a module, from its BUILD.gn:
        module_switch = defined(LOSCFG_XXX)
    Returns None when the module is unconditional."""
    bg = os.path.join(build_gn_dir, "BUILD.gn")
    if not os.path.exists(bg):
        return None
    with open(bg, errors="replace") as f:
        text = f.read()
    m = re.search(r"module_switch\s*=\s*defined\(\s*(LOSCFG_\w+)\s*\)", text)
    return m.group(1) if m else None


def parse_config_h(gn_out_dir):
    """Parse the GN genconfig output config.h (*fully expanded* Kconfig
    header — all symbols including `select`-derived ones) into an ordered
    dict {name: value}."""
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
    """config.h value form -> CMake set() value: 1 -> 1, quoted/other -> as-is."""
    if raw.startswith('"') and raw.endswith('"'):
        return raw[1:-1]
    return raw


def write_kconfig_values(path, values, config_used, header_src):
    """Write kconfig_values.cmake — a snapshot of the GN-expanded Kconfig
    values. The CMake layer consumes this instead of re-parsing the board
    .config text, so module switches and derived symbols (Kconfig select)
    match the GN build exactly. Also self-contained: immune to same-named
    LOSCFG_* variables leaking from the integrator's parent scope."""
    with open(path, "w") as f:
        f.write("#" + "=" * 77 + "\n")
        f.write("# AUTO-GENERATED by tools/export_cmake_sources.py — DO NOT EDIT.\n")
        f.write("# Snapshot of the GN genconfig output (config.h) for board config:\n")
        f.write(f"#   {config_used}\n")
        f.write(f"# Source header: {header_src}\n")
        f.write("# Contains the *expanded* Kconfig state, including symbols derived\n")
        f.write("# via `select` — which plain text parsing of the .config cannot see.\n")
        f.write(f"# {len(values)} LOSCFG_* symbols.\n")
        f.write("#" + "=" * 77 + "\n\n")
        f.write("set(EVOLVE_KCONFIG_SNAPSHOT_LOADED ON)\n")
        f.write(f'set(EVOLVE_KCONFIG_SNAPSHOT_HEADER "{header_src}")\n\n')
        for name, raw in values.items():
            v = h_to_cmake_value(raw)
            f.write(f'set({name} "{v}")\n')


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

    # Board config actually used by this GN gen (for provenance comments)
    config_used = "(unknown)"
    args_gn = os.path.join(args.gn_out, "args.gn")
    if os.path.exists(args_gn):
        with open(args_gn) as f:
            for ln in f:
                m = re.search(r'product_path\s*=\s*"([^"]+)"', ln)
                if m:
                    config_used = m.group(1) + "/kernel_configs/<type>.config"
                    break

    # Kconfig-expanded snapshot -> build/cmake/kconfig_values.cmake
    kconfig_values, config_header = parse_config_h(args.gn_out)
    values_path = os.path.join(
        os.path.dirname(args.output), "kconfig_values.cmake")
    if kconfig_values and config_header:
        write_kconfig_values(values_path, kconfig_values, config_used, config_header)
        print(f"[OK] wrote {values_path} ({len(kconfig_values)} symbols)")
        # Also snapshot the genconfig header verbatim — it becomes the
        # compile-time menuconfig.h for source-level integration (zero
        # conversion loss, immune to parent-scope variable leakage).
        snap_dir = os.path.dirname(values_path)
        import shutil
        shutil.copyfile(config_header,
                        os.path.join(snap_dir, "menuconfig.snapshot.h"))
        print(f"[OK] wrote {os.path.join(snap_dir, 'menuconfig.snapshot.h')}")

    groups = []  # (group_name, switch or None, [(cmake_path_expr, rel)])
    seen_files = set()
    for label in labels:
        try:
            info = gn_desc_json(gn, args.gn_out, label)
        except (subprocess.CalledProcessError, ValueError):
            continue  # targets gn desc cannot show — skip
        ttype = info.get("type", "")
        if ttype not in ("source_set", "static_library", "executable"):
            continue  # groups / configs carry no sources
        sources = info.get("sources", [])
        if not sources:
            continue
        src_dir = label_source_dir(label, args.gn_out)
        switch = parse_module_switch(src_dir)
        name = label[2:].replace("/", "_").replace(":", "_")
        rel_files = []
        for s in sources:
            if not s.startswith("//"):
                continue  # absolute/toolchain-internal paths
            rel = s[2:]
            if rel in seen_files:
                continue  # shared source compiled by several targets — keep once
            seen_files.add(rel)
            if rel.startswith("kernel/liteos_m/"):
                # inside the liteos_m repo — prefix with its top dir
                expr = "${EVOLVE_TOP_DIR}/" + rel[len("kernel/liteos_m/"):]
            else:
                # third_party / board files live under the GN source root
                expr = "${EVOLVE_SOURCE_ROOT}/" + rel
            rel_files.append((expr, rel))
        if rel_files:
            groups.append((name, switch, sorted(rel_files)))

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w") as f:
        f.write("#" + "=" * 77 + "\n")
        f.write("# AUTO-GENERATED by tools/export_cmake_sources.py — DO NOT EDIT.\n")
        f.write("# Regenerate after upgrading liteos_m or changing a board config:\n")
        f.write("#   gn gen <out> --args='...'\n")
        f.write("#   python3 tools/export_cmake_sources.py --gn-out <out>\n")
        f.write("#\n")
        f.write(f"# Generated from GN dep tree of //kernel/liteos_m:libkernel\n")
        f.write(f"# Modules: {len(groups)}   Source files: {len(seen_files)}\n")
        f.write("#\n")
        f.write("# Variables expected to be set by the includer:\n")
        f.write("#   EVOLVE_TOP_DIR      — liteos_m source root (this repo)\n")
        f.write("#   EVOLVE_SOURCE_ROOT  — GN source root hosting third_party/\n")
        f.write("#                          and the board dir (defaults to the\n")
        f.write("#                          parent of EVOLVE_TOP_DIR)\n")
        f.write("#   LOSCFG_* switches   — from the generated menuconfig config\n")
        f.write("#\n")
        f.write("# Each group is guarded by the LOSCFG_* switch parsed from the\n")
        f.write("# module's BUILD.gn (module_switch = defined(LOSCFG_...)).\n\n")
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

    total = sum(len(g[2]) for g in groups)
    print(f"[OK] wrote {args.output}")
    print(f"     {len(groups)} modules, {total} source files")
    for name, switch, files in groups:
        print(f"       {name:55s} {len(files):3d} files  {switch or '-'}")


if __name__ == "__main__":
    main()
