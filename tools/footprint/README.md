# footprint - LiteOS-M Kernel RAM Footprint Tool

**Version**: v1.6  |  中文版: [README_zh.md](README_zh.md)

Given standard build outputs, this tool automatically produces a kernel
RAM footprint report (Markdown + multi-sheet Excel) **for that exact
source revision and platform configuration**. Pure standard library, zero
third-party dependencies, Python 3.6+ only; the compilation probe optionally
uses the target cross toolchain (gcc + nm, preferring the compiler actually
recorded in the build outputs).

- **Static part** (code/constants/data segments/stacks): the linker map is
  the single source of truth, symbol-level traceable
- **Dynamic part** (control-block arrays and other runtime allocations):
  recomputed from the allocation formulas in the current source;
  **module switches and count limits are driven entirely by the platform
  configuration** - disabled modules are never counted
- **Memory-pool accounting**: the heap pool (g_memStart array / `.heap`
  segment) is shared infrastructure and excluded from RAM; the "memory pool
  layout" section shows carrier form

## 1. Requirements

| Item | Requirement | Notes |
|---|---|---|
| Python | 3.6+ | pure standard library |
| Kernel source tree | **the tool directory must live inside the kernel source tree** (e.g. `tools/footprint/`; two levels up is the kernel root) | dynamic formulas live-scan the kernel source; misplaced copies degrade all dynamic entries to warnings |
| Build output directory | contains `*.map` and `config.h` | standard OHOS build output directories satisfy this naturally |
| Cross toolchain | auto-discovered: compiler recorded in the build output's `toolchain.ninja` -> PATH -> source-tree `device/soc`/`prebuilts` search (`nm` also needed) | used by the compilation probe to measure `sizeof`; missing compiler when the probe is needed is a hard error |

## 2. Quick Start (One Command)

```bash
python3 footprint.py --out <build-output-dir>
# e.g. python3 footprint.py --out out/arm_mps2_an386/qemu_mini_system_demo
```

After running, first check the **inputs echo** to confirm the tool picked
the map and config you intended:

```
inputs: map=/path/to/OHOS_Image.map
        config=/path/to/config.h (auto-converted from config.h)
probe: 17 values measured by /path/to/cross-gcc
RAM 构成: 静态符号 17117 + 栈 4096 + 动态申请 41120 (+ 开销 288); 堆池 0 B 不计入 (共享基础设施, 见内存池布局)
RAM 合计 (不含节点开销): 62333 B (60.87 KB)
RAM 合计 (含节点开销): 62621 B (61.15 KB)
report written to ./footprint_report/report.md + footprint.xlsx + summary.json
```

The `RAM 构成` / `RAM 合计` lines print the size summary straight to the
terminal (numbers shown are for illustration); full details live in the
three output files.

Reports are written to `./footprint_report/` under the **directory where the
command is executed** (change with `--output`). A single run takes < 1 s
(measured: an386 0.45 s, ws63t 0.63 s, full pipeline including the probe).

**Rerun delta**: each run also writes `summary.json` (machine-readable
totals + per-group numbers + input provenance). Rerunning with the same
inputs into the same `--output` directory prints a delta table against the
previous run - build-to-build memory differences with zero extra
arguments. Runs with different inputs (e.g. another platform's map/config)
skip the comparison with a one-line note, never comparing across builds.

## 3. Input Discovery Rules

| Input | Discovery rule | On missing/ambiguity |
|---|---|---|
| map | the single `*.map` in the directory (top level first, then recursive) | none/multiple -> fail fast; multiple lists the candidates, disambiguate with `--map` |
| config | explicit `--config` -> top-level `.config` -> single recursive hit -> **`config.h` auto-conversion** | all missing -> error explaining the standard-directory requirement |

- `config.h` (an OHOS build artifact) is auto-converted to a macro
  environment: `1`->y, integers, strings; undefined macros are treated as
  disabled (fail-closed). The conversion is recorded in the report header
  and warning area, with the hint "Kconfig `.config` is the source of
  truth, re-verification recommended"
- Error policy: **missing input fails fast, no degraded guessing** -
  degradation equals wrong numbers

## 4. Accounting Rules

| Input | Role |
|---|---|
| linker map | static RAM ground truth (symbol level) |
| config (`.config` or `config.h`) | macro values and module switches |
| kernel source (the tree the tool lives in, fixed) | dynamic allocation formula anchors |
| board `target_config.h` | auto-discovered from map ownership and merged (board macros may exceed config) |

- RAM total = static symbols (`.data`+`.bss`) + stacks + dynamic allocations
  (rule c: the heap pool is shared infrastructure, excluded; stacks are
  fully counted via `.stack*`-prefixed and `*_stack`-suffixed segments)
- `.data` is counted once, on the RAM side
- dynamic entries are computed from source formulas:
  `count x sizeof(struct)`; size macros and types are measured by the
  compilation probe, which can also pull module-private headers
  (per-anchor `probe_headers`) and can be restricted to one assignment
  target (`lhs`) so sibling allocations in the same init function are
  never merged by mistake
- node overhead = per heap node `align_up(size,8)-size+header`, with
  the header derived from the build's own config (TLSF: 8
  +INTEGRITY_CHECK/LEAKCHECK/TASK_MEM_USED; BESTFIT: 16+HEAD_BACKUP/
  LEAKCHECK; e.g. an386=16B, ws63t=32B), listed separately

**Inclusion scope** (three-layer funnel; nothing outside is ever silently
dropped):

1. attributed to the kernel library: `libliteos_m.a(` or a path containing
   `kernel/liteos_m`
2. member prefix belongs to one of 12 kernel groups: base / arch / common /
   cpup / backtrace / exchook / signal / interrupt / hpl / trace / lms /
   power (posix, cmsis, osal etc. are excluded per the established scope,
   listed under "excluded" with reasons)
3. output-section classification: data/bss/stacks/heap (including
   RISC-V small-data `.sdata/.sbss` and `.got`; RAM-resident code like
   `.text_sram/.text_tcm` is counted as RAM)

Members attributed to the kernel library but not classifiable (unknown
prefixes, e.g. from newly added kernel modules) go to the "unrecognized
members" warning bucket, listed separately, never silently dropped.

## 5. Reading the Output

`report.md`, section by section:

| Section | Content |
|---|---|
| Summary | section class totals (RAM), RAM composition formula |
| Sub-module detail | 8 groups x section cross-table (RAM classes) |
| Dynamic allocations | per-entry size/status/formula/value source |
| Dynamic classification overview | items counted by value source: source-scanned / registered formulas (manual verification recommended) / map-true static / config-disabled |
| Memory pool layout | sys-heap carrier form (internal array/linker segment/free tail) and heap entry symbol |
| Section reconciliation | per RAM-relevant output section: header/attributed/fill/merged-relax/diff (trust check) |
| Excluded / warnings | funnel exclusions with reasons / runtime check results |

`footprint.xlsx` sheets: summary / RAM symbols / dynamic / excluded /
reconciliation (/unrecognized, only if non-empty). A
`summary.json` sidecar (totals + per-group + input provenance) serves rerun
deltas and CI consumption.

**Dynamic entry status**:

| Status | Meaning | Counted? |
|---|---|---|
| `dynamic` | source is `LOS_MemAlloc`, formula recomputed for this platform config | yes |
| `disabled` | config gate off (module not enabled) | no (sizes shown as `-`, listed under disabled) |
| `map-static` | symbol already in the map (static array form) | already in static totals, not double-counted |
| `source-static` | source is a static array but the map lacks it | version-mismatch warning |
| `unknown` | source form changed, anchor stale | no, warning to update anchors |

## 6. Runtime Self-Checks (Reported to the Warning Area)

- **Anchor self-check**: active `g_* = LOS_MemAlloc(...)` sites in this
  build's kernel source without anchor coverage produce a warning - new
  dynamic allocation sites are never silently missed; member-path
  left-values (e.g. `g_runtimeLogger.compressBuf`) are detected too and
  must be covered by an anchor registering that exact item (same-function
  anchors do not mask them); the scan also covers
  unknown-prefix bucket members (new code families land there first; their
  boot allocations must surface -- HPL was the 2026-10-27 precedent)
- **Section reconciliation**: warnings when section headers vs attributed
  records vs fill diverge beyond threshold (string merging/relax already
  deduplicated by address union)
- source or config newer than the map -> version-mismatch warning

## 7. How Module Switches Affect Results

Dynamic entry existence is driven by the platform config (anchor `when`
gates + source `#if` preprocessing, double insurance); switch platforms or
trim configs and rerun - no tool change needed. Examples (an386):

| Platform config | Tool behavior |
|---|---|
| RWSEM=y, LIMIT=6 | g_allRwsem = 6 x 16 = **96 B** counted as dynamic |
| RWSEM not configured | g_allRwsem -> disabled, not counted |
| DEBUG_MUTEX=y and MUX=y, LIMIT=24 | g_muxDebugArray = 24 x 16 = **384 B** |
| DEBUG on, module off | disabled (combined gate, all required) |

## 8. CLI Options

| Option | Description |
|---|---|
| `--out <dir>` | build output directory (the standard entry point; auto-discovers map/config) |
| `--map <file>` + `--config <file>` | fully explicit mode (CI / non-standard directories) |
| `--output <dir>` | report directory (default `./footprint_report`) |
| `--cc <cc>` | explicitly specify the cross compiler (default auto-discovery: build metadata -> PATH -> source-tree search) |
| `--sizeof-table <json>` | pre-measured `{'sz:TYPE'/'mac:NAME': int}` table replacing the compile probe -- offline reproduction of archived builds, no toolchain needed |

Exit codes: `0` = report written (warnings, if any, are in the report);
`1` = fatal error, no report.

## 9. Directory Layout

```
tools/footprint/
├── footprint.py    # entry: CLI parsing, input discovery, shared pipeline (run_pipeline)
└── src/
    ├── input.py    # input parsing: .config/config.h/board merge, GNU map, #if preprocessing, formula evaluator
    ├── anchors.py  # dynamic allocations: anchor table, source live-scanning, compilation probe, evaluation engine
    ├── scope.py    # scope and summary: section classification, three-layer funnel, heap-pool exclusion, grouping, reconciliation
    └── output.py   # report output: Markdown + Excel (pure stdlib zip+XML)
```

Dependencies are one-way: `input <- anchors <- scope <- output`.

## 10. FAQ

**Q: Error "no config found ... (neither Kconfig .config nor generated config.h)"?**
The directory has neither `.config` nor `config.h` - not a standard build
output directory. Make sure you pass the `out/<platform>/<component>` level,
or switch to fully explicit `--map` + `--config`.

**Q: Is the config.h auto-conversion trustworthy?**
The conversion semantics match Kconfig and are fail-closed; the risk is the
source (artifacts may contain macros injected by the build flow). The report
warning area advises re-verification; a Kconfig `.config` placed alongside
is picked up automatically and takes priority.

**Q: Warning "kernel dynamic allocation not in the anchor table"?**
The kernel gained a persistent allocation site the anchor table does not
cover. Report it to the maintainer, or add an entry to `DEFAULT_ANCHORS` in
`src/anchors.py`.

**Q: No cross toolchain?**
When dynamic entries need the probe but no compiler is found, the tool
fails outright (no totals with missing numbers). Run it on a machine with a
build environment - the tool auto-locates the compiler from build metadata
(`toolchain.ninja`) and the source tree; special environments can use
`--cc`. For archived builds you can also pass a pre-measured value table
(`--sizeof-table`), which fully bypasses the probe.

**Q: Want to adjust the inclusion/exclusion scope?**
Edit the scope table (`DEFAULT_SCOPE`) in `src/scope.py`, or report the
requirement to the maintainer.

## 11. Limitations

- dynamic formula anchors evolve with the source; documented scope
  differences may exist versus historical manual baselines
- the anchor self-check only sees direct assignments to file-scope
  globals and their member paths (`g_x = ...`, `g_x.member = ...`);
  allocations into local variables, out-parameters or helper functions
  (e.g. task stacks via the generic creation pipeline) stay invisible
  to it and rely on the registered anchors
- indirect allocations (memory requested by the kernel through
  shells/components) are not in the anchor table
- LOS_MemAllocAlign padding (0..7B per aligned item) depends on pool
  state at runtime and is not modeled
- allocations triggered by product API calls (LOS_QueueCreate,
  LOS_TaskHplSizeSet, ...) are product-side occupancy by design and
  excluded from the kernel total (not listed in the report)
- multi-core scenarios use single-core accounting
- builds without function-sections get input-section granularity in the
  static detail
