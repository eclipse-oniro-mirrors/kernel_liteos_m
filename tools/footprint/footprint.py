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
"""footprint - LiteOS-M kernel RAM footprint scanner."""

# re-exports: programmatic access for tests
__all__ = ["DEFAULT_ANCHORS", "DEFAULT_SCOPE", "main", "run_pipeline"]

import argparse
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "src"))

from input import (REPO_ROOT, apply_board_config,
                   detect_board_dirs, die, parse_config_h, parse_dotconfig,
                   parse_map)
from anchors import (DEFAULT_ANCHORS, find_build_cc, process_anchors,
                     scan_uncovered_allocs)
from scope import (DEFAULT_SCOPE, aggregate_kernel, build_summary,
                   exclude_sys_heap_pool, node_header_for,
                   outsec_reconciliation, run_funnel)
from output import load_summary, summary_lines, unresolved_note, \
    write_report, write_summary, write_xlsx_report


def _out_config(outdir):
    """Config discovery for --out mode: top-level .config/config, then a
    unique recursive .config, then the generated config.h (top level,
    then unique recursive). Returns (path, is_config_h) or (None, False)
    when no config was found."""
    for cand in (outdir / ".config", outdir / "config"):
        if cand.is_file():
            return cand, False
    cands = sorted(str(p) for p in outdir.rglob(".config"))
    if len(cands) == 1:
        return Path(cands[0]), False
    if len(cands) > 1:
        die("multiple .config found under %s:\n  %s\n"
            "use --config to pick one" % (outdir, "\n  ".join(cands)))
    if (outdir / "config.h").is_file():
        return outdir / "config.h", True
    cands = sorted(str(p) for p in outdir.rglob("config.h"))
    if len(cands) == 1:
        return Path(cands[0]), True
    return None, False


def discover_out(outdir, config_arg=None):
    """Auto-discover the linker map and configuration under a build
    output directory -- the single customer entry point.

    map:    exactly one *.map (top level first, then recursive).
    config: an explicit --config wins; else .config/config at the top
            level; else exactly one recursive match; else the OHOS
            generated config.h (top level, then unique recursive),
            converted internally with provenance recorded in the report.
    Returns (map_path, config_path, is_config_h).
    """
    outdir = Path(outdir)
    if not outdir.is_dir():
        die("--out %s is not a directory" % outdir)
    maps = sorted(str(p) for p in outdir.glob("*.map"))
    if not maps:
        maps = sorted(str(p) for p in outdir.rglob("*.map"))
    if not maps:
        die("no *.map found under %s" % outdir)
    if len(maps) > 1:
        die("multiple maps found under %s:\n  %s\nuse --map to pick one"
            % (outdir, "\n  ".join(maps)))
    if config_arg:
        return maps[0], Path(config_arg), False
    cfg, is_config_h = _out_config(outdir)
    if cfg is None:
        die("no config found under %s (neither Kconfig .config "
            "nor generated config.h)\n"
            "a standard OHOS build output directory contains "
            "both *.map and config.h" % outdir)
    return maps[0], cfg, is_config_h


_DELTA_ROWS = (
    ("RAM static", "ram_static"),
    ("stack", "stack"),
    ("dynamic", "dyn"),
    ("overhead", "overhead"),
    ("RAM total", "ram_total"),
)


def _print_delta(prev, s, map_path, config_path):
    """Rerun delta against the previous summary.json in the same --output.

    Comparison is only offered for the same inputs (map AND config paths):
    different platforms / targets are skipped with one line, never
    compared across builds.
    """
    oi = prev.get("inputs") or {}
    if oi.get("map") != str(map_path) or oi.get("config") != str(config_path):
        print("delta skipped: previous run used different inputs "
              "(map: %s, config: %s)" % (oi.get("map"), oi.get("config")))
        return
    old = prev.get("totals") or {}
    print("delta vs previous run in this output directory:")
    for label, key in _DELTA_ROWS:
        if key not in old:
            continue
        print("  %-11s %6d -> %-6d (%+d B)"
              % (label + ":", old[key], getattr(s, key),
                 getattr(s, key) - old[key]))


def _pipe_inputs(map_path, config_path, cfg_is_h):
    """Parse config + map, detect/merge the board config."""
    if cfg_is_h:
        macros, _notset = parse_config_h(config_path)
    else:
        macros, _notset = parse_dotconfig(config_path)
    recs, outsec_sizes, outsec_fills, outsec_relax = parse_map(map_path)
    if not recs:
        die("no attributed input sections parsed from %s" % map_path)
    # board target_config.h adds macros beyond the Kconfig .config
    board_dirs = detect_board_dirs(recs)
    board_merged = 0
    if board_dirs:
        macros, board_merged = apply_board_config(board_dirs, macros)
    return (macros, recs, outsec_sizes, outsec_fills, outsec_relax,
            board_dirs, board_merged)


def _pipe_warnings(map_path, config_path, cfg_is_h, macros, recs,
                   outsec_sizes, outsec_fills, outsec_relax, scope,
                   pool_carrier, funnel, cc, sizeof_table, board_dirs):
    """Collect deduped run warnings, run the anchors and reconciliation.

    Returns (items, probe_values, warnings, sec_recon, map_mtime,
    config_mtime).
    """
    warnings = []
    seen_warn = set()

    def add_warn(msg):
        if msg not in seen_warn:
            seen_warn.add(msg)
            warnings.append(msg)

    map_mtime = os.path.getmtime(map_path)
    config_mtime = os.path.getmtime(config_path)
    if cfg_is_h:
        add_warn("config 来源为构建生成物 config.h (已自动转换, %d 宏); "
                 "Kconfig .config 为事实源, 建议用其复核" % len(macros))
    if pool_carrier and pool_carrier.get("expected") is not None and \
            pool_carrier["expected"] != pool_carrier["size"]:
        add_warn("g_memStart 尺寸 %d B 与 LOSCFG_SYS_HEAP_SIZE=%d 不一致 "
                 "(剔除仍按 map 记录)"
                 % (pool_carrier["size"], pool_carrier["expected"]))
    for a in DEFAULT_ANCHORS:
        srcf = REPO_ROOT / a["file"]
        if srcf.exists() and srcf.stat().st_mtime > map_mtime:
            add_warn("源码 %s 比 map 新, 动态公式可能与该次构建不一致"
                     % a["file"])
    if config_mtime > map_mtime:
        add_warn("config 比 map 新, 宏值可能与该次构建不一致")
    items, probe_values = process_anchors(
        DEFAULT_ANCHORS, recs, macros, scope, REPO_ROOT, cc, sizeof_table,
        warnings, board_dirs)
    if probe_values:
        print("probe: %d values measured by %s" % (len(probe_values), cc))
    n_unres = len([it for it in items
                   if it.size is None
                   and it.status in ("dynamic", "source-static")
                   and not it.transient])
    if n_unres:
        add_warn("%d 项动态取值未解析, RAM 合计不含这些项" % n_unres)
    # anchor-coverage self-check: kernel-scope active allocations that no
    # anchor covers (never-silent principle applied to the dynamic side)
    for rel, var, func in scan_uncovered_allocs(funnel, DEFAULT_ANCHORS,
                                                macros, REPO_ROOT):
        add_warn("内核动态分配未入锚点表: %s: %s (函数 %s, 建议补锚点)"
                 % (rel, var, func or "?"))
    # linker-dimension reconciliation + unexplained-residual warnings
    sec_recon = outsec_reconciliation(recs, outsec_sizes, outsec_fills,
                                      outsec_relax, scope)
    for r in sec_recon:
        if r["warn"]:
            add_warn("段对账差额异常: %s 段头 %d B vs 归属+fill %d B (差 %d B)"
                     % (r["name"], r["header"], r["attr"] + r["fill"],
                        r["diff"]))
    return (items, probe_values, warnings, sec_recon,
            map_mtime, config_mtime)


def _pipe_output(outdir, cfg_is_h, map_path, config_path, macros, scope,
                 funnel, groups, symbols, items, outsec_sizes, probe_values,
                 sizeof_table, warnings, map_mtime, config_mtime, board_dirs,
                 board_merged, sec_recon, pool_carrier):
    """Write xlsx/report/summary into `outdir` + terminal echo."""
    outdir.mkdir(parents=True, exist_ok=True)
    prev = load_summary(outdir)
    write_xlsx_report(outdir, map_path, config_path, groups, symbols, items,
                      funnel, outsec_sizes, scope, sec_recon)
    board_info = ", ".join("%s/target_config.h (+%d 宏)"
                           % (d, board_merged) for d in board_dirs) or \
        "未检测到 (仅 .config 宏)"
    write_report(outdir, cfg_is_h, map_path, config_path, macros, scope,
                 funnel, groups, symbols, items, outsec_sizes, probe_values,
                 sizeof_table, warnings, map_mtime, board_info,
                 sec_recon=sec_recon, pool_carrier=pool_carrier)
    s = build_summary(groups, items, outsec_sizes, scope)
    write_summary(outdir, s, groups, map_path, config_path,
                  map_mtime, config_mtime)
    comp, tot, tot_ov = summary_lines(s)
    note = unresolved_note(items)
    print(comp)
    print(tot + note)
    print(tot_ov + note)
    if prev:
        _print_delta(prev, s, map_path, config_path)
    print("report written to %s/report.md + footprint.xlsx + summary.json"
          % outdir)
    return s


def run_pipeline(map_path, config_path, cfg_is_h, outdir,
                 cc_arg=None, sizeof_table=None, build_dir=None):
    """Full analysis pipeline for one (map, config) pair.

    Parses the inputs, runs the funnel / anchors / reconciliation,
    writes report.md + footprint.xlsx + summary.json into `outdir`,
    and prints the terminal echo (inputs, probe, summary lines, delta).
    This is the single pipeline behind the CLI and the programmatic
    API used by tests -- the two can never drift apart.

    sizeof_table: pre-measured values {'sz:TYPE': bytes, 'mac:NAME': v}
    that fully replace the compile probe (offline reproduction of
    archived builds on machines without a cross toolchain).
    Returns the Summary namedtuple.
    """
    map_path = str(map_path)
    config_path = str(config_path)
    outdir = Path(outdir)
    if sizeof_table is None:
        sizeof_table = {}
    print("inputs: map=%s" % map_path)
    print("        config=%s%s" % (config_path,
                                   " (auto-converted from config.h)"
                                   if cfg_is_h else ""))
    scope = dict(DEFAULT_SCOPE)
    (macros, recs, outsec_sizes, outsec_fills, outsec_relax,
     board_dirs, board_merged) = _pipe_inputs(map_path, config_path, cfg_is_h)
    # node-header model follows this build's allocator config
    # (16 B on an386: TLSF + INTEGRITY_CHECK + TASK_MEM_USED)
    scope["node_header_bytes"], scope["node_header_basis"] = \
        node_header_for(macros)
    if build_dir is None:
        build_dir = str(Path(map_path).parent)
    cc = find_build_cc(build_dir, macros, cc_arg)
    funnel = run_funnel(recs, scope)
    # D18: the sys-heap pool carrier array is shared
    # infrastructure, not kernel static data
    pool_carrier = exclude_sys_heap_pool(funnel, scope, macros)
    groups, symbols = aggregate_kernel(funnel.kernel, scope)
    (items, probe_values, warnings, sec_recon,
     map_mtime, config_mtime) = _pipe_warnings(
        map_path, config_path, cfg_is_h, macros, recs, outsec_sizes,
        outsec_fills, outsec_relax, scope, pool_carrier, funnel, cc,
        sizeof_table, board_dirs)
    s = _pipe_output(outdir, cfg_is_h, map_path, config_path, macros, scope,
                     funnel, groups, symbols, items, outsec_sizes,
                     probe_values, sizeof_table, warnings, map_mtime,
                     config_mtime, board_dirs, board_merged, sec_recon,
                     pool_carrier)
    return s


def _load_sizeof_table(path):
    """Load and validate a --sizeof-table JSON (fail closed)."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            table = json.load(f)
    except (OSError, ValueError) as e:
        die("cannot load --sizeof-table %s: %s" % (path, e))
    if not isinstance(table, dict) or \
            not all(isinstance(k, str) and isinstance(v, int)
                    for k, v in table.items()):
        die("--sizeof-table must be a JSON object of "
            "{'sz:TYPE': bytes, 'mac:NAME': value} (int values)")
    return table


def main():
    ap = argparse.ArgumentParser(
        description="LiteOS-M kernel RAM footprint scanner",
        epilog="quick start:\n"
               "  python3 footprint.py --out <build-output-dir>\n"
               "  auto-finds map/config/toolchain; writes ./footprint_report/\n"
               "  (report.md + footprint.xlsx).  In the report check the 'inputs:'\n"
               "  echo, then the reconciliation table (diff column = zeros).\n"
               "  Exit codes: 0 = report written; 1 = fatal error.\n"
               "  See README.md for accounting rules and FAQ.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--out", help="build output directory (auto-find map/.config)")
    g.add_argument("--map", help="linker map file")
    ap.add_argument("--config", help="Kconfig .config used for that build")
    ap.add_argument("--output", default="footprint_report",
                    help="report output directory (default ./footprint_report)")
    ap.add_argument("--cc", help="cross compiler for the sizeof probe")
    ap.add_argument("--sizeof-table", metavar="JSON", dest="sizeof_table",
                    help="pre-measured {'sz:TYPE'/'mac:NAME': int} table "
                         "replacing the compile probe (offline reproduction "
                         "of archived builds, no toolchain needed)")
    args = ap.parse_args()

    if args.out:
        map_path, config_path, cfg_is_h = discover_out(args.out, args.config)
    else:
        if not args.config:
            die("--map requires --config")
        map_path, config_path, cfg_is_h = args.map, args.config, False
    if not os.path.isfile(map_path):
        die("map not found: %s" % map_path)
    if not os.path.isfile(config_path):
        die("config not found: %s" % config_path)

    sizeof_table = {}
    if args.sizeof_table:
        sizeof_table = _load_sizeof_table(args.sizeof_table)

    run_pipeline(map_path, config_path, cfg_is_h, args.output,
                 cc_arg=args.cc, sizeof_table=sizeof_table,
                 build_dir=args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
