# liteos_m 升级清单

升级 `liteos_m` 上游 fork 时（重新同步到更新的 OpenHarmony tag，或 rebase 本地提交），按顺序过此清单。构建框架的快照消费设计意味着大部分上游变更经已提交的导出落地（`sources.cmake`、`kconfig_values.cmake`、`menuconfig.snapshot.h`），但若干分叉点需要人工介入。

> **摘要** ——共 **12 个验证点**，分 6 个阶段。
> 阶段 2（快照分叉）有配置期守护，将最危险的泄漏转为 `FATAL_ERROR`；其余为人工。

---

## 前置条件

需要一个能运行 **GN** 的环境（用于重新生成已提交的导出）。集成方绝不运行 GN，但执行升级的维护者需要。hs-fbb 的 `src/` 树 + GN 工具链即可。

---

## 阶段 1 —— 重新生成已提交的导出

从 liteos_m 仓根运行 `tools/export_cmake_sources.py`（遍历
`gn desc --tree //kernel/liteos_m:libkernel` 并写出）：

| 产物 | 携带内容 |
|---|---|
| `build/cmake/sources.cmake` | 源码列表（79 引用，6 个模块守护 `if(LOSCFG_*)` 块） |
| `build/cmake/kconfig_values.cmake` | 79 个 `LOSCFG_*` CMake 变量（GN 展开） |
| `build/cmake/menuconfig.snapshot.h` | 同值 C 宏（参考快照，**不**注入） |

**提交这三个文件。** 它们是构建关于"编译哪些文件、哪些模块开启"的真理来源。

---

## 阶段 2 —— 快照分叉守护（4 点，自动检查）

GN 基线与 fbb 调优的 `target_config.h` 在 4 个关键时序/容量值上分叉。`target_config.h` 覆盖它们；快照基线**不得**泄漏。

| `LOSCFG_*` | 基线（快照） | fbb（target_config.h） | 泄漏症状 |
|---|---|---|---|
| `BASE_CORE_TICK_PER_SECOND` | 100 | 1000 | tick 从 1000 跌至 100Hz |
| `BASE_CORE_TIMESLICE_TIMEOUT` | 20000 | 2000 | 时间片 2000→20000（10×） |
| `HWI_PRIO_LIMIT` | 32 | 7 | CLIC `#error` 或优先级计算错误 |
| `CPUP_SAMPLE_PERIOD` | 100 | 10000 | CPUP 窗口 10s→0.1s |

**自动守护**：`liteos_m/CMakeLists.txt` 在配置期运行分叉守护（通过
`EVOLVE_DIVERGE_GUARD` 变量，默认为上述 4 符号）。对每个符号检查
`target_config.h` 仍有 `#define`：

- **缺失 `#define`** → 配置期 `FATAL_ERROR`（上游默认会泄漏 →"启动一次 OK 后静默"）。恢复覆盖。
- **值匹配基线** → `WARNING`（fbb 调优被误回退，或快照重新生成到 fbb 值）。确认意图；若有意，更新 `menuconfig.snapshot.h` 警告头与本清单。
- **值分叉** → `STATUS` "OK" 行，无需操作。

**重新生成后操作**：运行 `python3 build.py diting-community-kernel-lm`，确认 4 行 "divergence guard: … OK" 出现。若有 `FATAL_ERROR`，恢复 `target_config.h` 的 `#define`。

---

## 阶段 3 —— Gap-fill 源码（3 点，人工）

`sources.cmake` 只采集 GN 图中从 `//kernel/liteos_m:libkernel` 可达的源。3 个 3322 专属源不在该图中，在 `liteos_m/CMakeLists.txt` 中经 `EVOLVE_EXTRA_SOURCES` 变量手工追加（默认值 = 下列 3 源；板级经变量覆盖）：

| 源 | gap-fill 原因 |
|---|---|
| `arch/risc-v/src/los_trap.S` | 异常 trap 入口，不在 libkernel GN 目标中 |
| `drivers/interrupt/riscv_lcmp_clic.c` | HI3322 CLIC 驱动，板级专属 |
| `drivers/timer/dw_apb_timer.c` | 厂商 DW-APB tick，板级专属 |

**重新生成后操作**：确认这 3 个源现在仍未出现在 `sources.cmake` 中（若导出器开始采集它们，`EVOLVE_EXTRA_SOURCES` 默认值会导致重复编译 → 重定义错误）。

```sh
grep -E "los_trap\.S|riscv_lcmp_clic|dw_apb_timer" build/cmake/sources.cmake
# 期望：无匹配
```

---

## 阶段 4 —— 模块禁用（4 点，人工）

`liteos_m/CMakeLists.txt` 经 `EVOLVE_MODULE_DISABLES` 变量强制禁用 4 个模块（默认值 = 下列 4 个；板级覆盖）。这些与 prebuilt `libc.a` 冲突或在旧清单中缺失：

| `LOSCFG_*` 置 "0" | 原因 |
|---|---|
| `POSIX_API` | fork 的 `lib/posix/src/` 重新实现 pthread/sem/mqueue → 与 `libc.a` 符号重复 |
| `FS_VFS` | HI3322 无文件系统；旧清单省略 |
| `KERNEL_SIGNAL` | 旧清单省略；`libc.a` 提供 signal 函数 |
| `PLATFORM_EXC` | 旧清单省略（`kernel/base` 的 `los_exc.c` 覆盖基础） |

**重新生成后操作**：确认 `CMakeLists.txt` 的 `EVOLVE_MODULE_DISABLES` 默认值仍有效。若升级重命名了符号，更新默认值。若 `sources.cmake` 中某新源依赖这些模块之一且编译失败，加入阶段 5 的排除列表。

---

## 阶段 5 —— 源码排除（3 点，人工）

`liteos_m/CMakeLists.txt` 经 `EVOLVE_EXCLUDE_SOURCES` 变量移除 3 个源（默认值 = 下列 3 源；板级覆盖）。它们依赖 `target_config.h` 置 0 或未定义的配置项：

| 排除的源 | 所需守护 |
|---|---|
| `kernel/base/mem/common/memstat/los_memstat.c` | `LOSCFG_MEM_TASK_STAT`（未定义） |
| `kernel/base/mem/slab/los_slab.c` | `LOSCFG_KERNEL_MEM_SLAB_EXTENTION`（未定义；`LOSCFG_KERNEL_MEM_SLAB=0`） |
| `kernel/base/mem/slab/los_slabmem.c` | `SLAB_MEM_COUNT` / `LosSlabControlHeader`（同守护） |

**重新生成后操作**：确认 `CMakeLists.txt` 的 `EVOLVE_EXCLUDE_SOURCES` 默认值仍匹配真实路径（上游可能移动了文件）。若构建在某个守护后的未知类型上失败，将新文件加入排除列表。若 `target_config.h` 现在定义了这些守护之一，移除对应排除项（让该源编译）。

---

## 阶段 6 —— 构建 + 二进制大小验证

```sh
cd src && python3 build.py diting-community-kernel-lm
```

**成功标准**：

1. 构建完成（增量约 5–10s，全量重配置更久）。
2. `application.bin` 大小 ≈ **191792 字节**（md5 基线
   `f8f3a5202bb6f438c688ae2f5a07f260`）。少量字节漂移对琐碎上游变更可接受；千字节级漂移意味着模块/符号静默变更——接受前调查。
3. 配置输出出现 4 行 "divergence guard … OK" STATUS。

构建失败时：
- **重定义错误** → 阶段 3（gap-fill 重复）或阶段 4（模块禁用缺失）。
- **`#ifdef` 后未声明标识符** → 阶段 5（新源需排除，或 `target_config.h` 需加守护）。
- **CLIC `#error LOSCFG_HWI_PRIO_LIMIT exceeds…`** → 阶段 2（HWI_PRIO 覆盖丢失）。

---

## 双仓协调

触及集成契约（`EVOLVE_*` 变量、`march`/`mabi`、board 路径）的变更需在两个仓**配对提交**：

| 仓 | 路径 | 分支 | MR |
|---|---|---|---|
| `kernel_liteos_m`（内） | `src/kernel/liteos_m/` | `diting-kernel-lm` | gitcode MR#4 |
| `hs-fbb`（外） | `src/kernel/liteos_m_port/`、`src/kernel/CMakeLists.txt` | `diting-kernel-lm` | gitcode MR#1 |

两者均为 fast-forward 目标。在外仓提交信息中交叉引用内仓 commit hash（反之亦然），使配对可追溯。

---

## 速查 —— 12 个点

| # | 阶段 | 点 | 自动？ |
|---|---|---|---|
| 1 | 1 | 重新生成 sources.cmake | 脚本 |
| 2 | 1 | 重新生成 kconfig_values.cmake | 脚本 |
| 3 | 1 | 重新生成 menuconfig.snapshot.h | 脚本 |
| 4 | 2 | TICK_PER_SECOND 覆盖存在 | **守护** |
| 5 | 2 | TIMESLICE_TIMEOUT 覆盖存在 | **守护** |
| 6 | 2 | HWI_PRIO_LIMIT 覆盖存在 | **守护** |
| 7 | 2 | CPUP_SAMPLE_PERIOD 覆盖存在 | **守护** |
| 8 | 3 | gap-fill 3 源不在 sources.cmake | 人工 |
| 9 | 4 | 4 个模块禁用仍有效 | 人工 |
| 10 | 5 | 3 个源排除仍匹配路径 | 人工 |
| 11 | 5 | target_config.h 排除项守护未变 | 人工 |
| 12 | 6 | application.bin 字节级 sanity | 人工 |

点 4–7 现已配置期强制；其余仍为人工。
