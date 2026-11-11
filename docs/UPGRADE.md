# liteos_m 升级清单

升级 `liteos_m` 上游 fork 时（重新同步到更新的 OpenHarmony tag，或 rebase 本地提交），按顺序过此清单。构建框架的快照消费设计意味着大部分上游变更经已提交的导出落地（`sources.cmake`、`kconfig_values.cmake`、`menuconfig.snapshot.h`），但若干分叉点需要人工介入。

> **摘要** ——共 **14 个验证点**，分 6 个阶段。
> 阶段 2（快照分叉）与阶段 4（模块门禁）有配置期守护/gate sync，将最危险的泄漏转为 `FATAL_ERROR`/自动跟随；其余为人工。

---

## 前置条件

需要一个能运行 **GN** 的环境（用于重新生成已提交的导出）。集成方绝不运行 GN，但执行升级的维护者需要。hs-fbb 的 `src/` 树 + GN 工具链即可。

---

## 阶段 1 —— 重新生成已提交的导出

从 liteos_m 仓根运行 `tools/export_cmake_sources.py`（遍历
`gn desc --tree //kernel/liteos_m:libkernel` 并写出）：

| 产物 | 携带内容 |
|---|---|
| `build/cmake/sources.cmake` | 源码列表（281 条目，9 个模块守护 `if(LOSCFG_*)` 块） |
| `build/cmake/kconfig_values.cmake` | 84 个 `LOSCFG_*` CMake 变量（GN 展开） |
| `build/cmake/menuconfig.snapshot.h` | 同值 C 宏（参考快照，**不**注入） |

**提交这三个文件。** 它们是构建关于"编译哪些文件、哪些模块开启"的真理来源。
手写契约（gap-fill、musl 补源、mem 子树守卫）在 `build/cmake/sources_extra.cmake`
——**不在导出之列**，重导出不会触碰它。

---

## 阶段 2 —— 快照分叉守护（5 点，自动检查）

GN 基线与 fbb 调优的 `target_config.h` 在 5 个关键时序/容量值上分叉。`target_config.h` 覆盖它们；快照基线**不得**泄漏。

| `LOSCFG_*` | 基线（快照） | fbb（target_config.h） | 泄漏症状 |
|---|---|---|---|
| `BASE_CORE_TICK_PER_SECOND` | 100 | 1000 | tick 从 1000 跌至 100Hz |
| `BASE_CORE_TIMESLICE_TIMEOUT` | 20000 | 2000 | 时间片 2000→20000（10×） |
| `HWI_PRIO_LIMIT` | 32 | 7 | CLIC `#error` 或优先级计算错误 |
| `CPUP_SAMPLE_PERIOD` | 100 | 10000 | CPUP 窗口 10s→0.1s |
| `BASE_IPC_QUEUE_LIMIT` | 30 | 64 | 队列限额 64→30 |

**自动守护**：`liteos_m/CMakeLists.txt` 在配置期运行分叉守护（通过
`EVOLVE_DIVERGE_GUARD` 变量，默认为上述 5 符号）。对每个符号检查
`target_config.h` 仍有 `#define`：

- **缺失 `#define`** → 配置期 `FATAL_ERROR`（上游默认会泄漏 →"启动一次 OK 后静默"）。恢复覆盖。
- **值匹配基线** → `WARNING`（fbb 调优被误回退，或快照重新生成到 fbb 值）。确认意图；若有意，更新 `menuconfig.snapshot.h` 警告头与本清单。
- **值分叉** → `STATUS` "OK" 行，无需操作。

**重新生成后操作**：运行 `python3 build.py diting-community-kernel-lm`，确认 4 行 "divergence guard: … OK" 出现。若有 `FATAL_ERROR`，恢复 `target_config.h` 的 `#define`。

---

## 阶段 3 —— Gap-fill 源码（3 点，人工）

`sources.cmake` 只采集 GN 图中从 `//kernel/liteos_m:libkernel` 可达的源。3 个 3322 专属源不在该图中，在 `build/cmake/sources_extra.cmake` 经 `EVOLVE_EXTRA_SOURCES` 变量手工追加（默认值 = 下列 3 源；板级经变量覆盖）：

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

### 本地 fork 补丁（1 点，人工）

`arch/risc-v/include/arch/cache.h` 带一处本地补丁：`#include <stdint.h>`
（公共头自含性修复——osal_cache.c 等内核外消费方首个头就是它，没有这行
拿不到 `uintptr_t`；可上游化）。原 liteos_m_port/include/arch/cache.h
影子副本已删除，`<arch/cache.h>` 现在直接解析到本文件。

**上游重同步后操作**：确认该 include 仍在（冲突时保本地版）。若上游自身
补了自含性，删除本地补丁即可。

```sh
grep -n "stdint" arch/risc-v/include/arch/cache.h
# 期望：命中
```

---

## 阶段 4 —— 模块门禁（自动 gate sync + 1 个人工点）

模块的开/关**不再**由 `EVOLVE_MODULE_DISABLES` 手工清单维护：
`liteos_m/CMakeLists.txt` 配置期运行 gate sync
（`build/cmake/target_config_sync.cmake`），把 `sources*.cmake`（自动
快照 + 手写契约）里每个 `if(LOSCFG_*)` 门禁重置为 `target_config.h` 的值：

| `target_config.h` | 模块源码 |
|---|---|
| `#define LOSCFG_X 1` | 编入 |
| `#define LOSCFG_X 0` / 未定义 | 剔除（与 los_config.h 默认 OFF 语义一致） |

3322 现状：SIGNAL / PLATFORM_EXC / MEM_TASK_STAT / SLAB_EXTENTION 在
`target_config.h` 中未定义 → 自动剔除（`EVOLVE_MODULE_DISABLES` 默认已
置空，仅作集成方强制关停锤保留；mem 子树守卫见阶段 5）。

**重新生成后操作**：确认配置输出出现一行
`liteos_m gate sync: N module gates ... target_config.h`，且 N 覆盖
`sources*.cmake` 的全部门禁。若升级**新增**了带 `LOSCFG_*` 守卫的模块组，
同步会在下个构建自动跟随 `target_config.h`——但要人工确认该开关在
`target_config.h` 中的取值符合预期（新符号默认未定义 = 关）。

---

## 阶段 5 —— mem 子树守卫（3 点，人工）

`kernel/base/BUILD.gn` 在目标内部按 `if(defined(LOSCFG_*))` 守护这 3 个源，`gn desc` 导出时被基线求值掉（3322 开 → 平铺进快照）。`build/cmake/sources_extra.cmake` 把它们从快照摘出、按原守卫放回；`target_config.h` 沉默的板（含 3322）经 gate sync 自动剔除——`EVOLVE_EXCLUDE_SOURCES` 默认已退役为空，仅作硬排除锤子保留。

| 摘出重守卫的源 | 守卫 |
|---|---|
| `kernel/base/mem/common/memstat/los_memstat.c` | `LOSCFG_MEM_TASK_STAT` |
| `kernel/base/mem/slab/los_slab.c` | `LOSCFG_KERNEL_MEM_SLAB_EXTENTION` |
| `kernel/base/mem/slab/los_slabmem.c` | `LOSCFG_KERNEL_MEM_SLAB_EXTENTION`（同上） |

**重新生成后操作**：确认 `sources_extra.cmake` 里这 3 个路径仍匹配真实文件（上游可能移动了文件）。路径失配时 `REMOVE_ITEM` 静默无效：守卫 OFF 的板会让旧路径文件平铺回编译列表（静默漂移，靠阶段 6 大小基线兜底）；守卫 ON 的板在 `add_library` 处配置期报错（当场可见）。上游移动了文件就同步更新守卫路径。

---

## 阶段 6 —— 构建 + 二进制大小验证

```sh
cd src && python3 build.py diting-community-kernel-lm
```

**成功标准**：

1. 构建完成（增量约 5–10s，全量重配置更久）。
2. `application.bin` 大小与 md5 与升级前基线一致（逐字节对比最近一次
   已知良好构建；千字节级漂移意味着模块/符号静默变更——接受前调查）。
3. 配置输出出现 5 行 "divergence guard … OK" STATUS。
4. 配置输出出现 1 行 "gate sync: … module gates … target_config.h" STATUS。

构建失败时：
- **重定义错误** → 阶段 3（gap-fill 重复）或模块被 gate sync 误开
  （查 `target_config.h` 对应开关）。
- **`#ifdef` 后未声明标识符** → 阶段 5（`EVOLVE_EXCLUDE_SOURCES` 排除，或 `target_config.h` 加守护）。
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

## 速查 —— 14 个点

| # | 阶段 | 点 | 自动？ |
|---|---|---|---|
| 1 | 1 | 重新生成 sources.cmake | 脚本 |
| 2 | 1 | 重新生成 kconfig_values.cmake | 脚本 |
| 3 | 1 | 重新生成 menuconfig.snapshot.h | 脚本 |
| 4 | 2 | TICK_PER_SECOND 覆盖存在 | **守护** |
| 5 | 2 | TIMESLICE_TIMEOUT 覆盖存在 | **守护** |
| 6 | 2 | HWI_PRIO_LIMIT 覆盖存在 | **守护** |
| 7 | 2 | CPUP_SAMPLE_PERIOD 覆盖存在 | **守护** |
| 8 | 2 | QUEUE_LIMIT 覆盖存在 | **守护** |
| 9 | 3 | gap-fill 3 源不在 sources.cmake | 人工 |
| 10 | 3 | fork cache.h 的 stdint 本地补丁仍在 | 人工 |
| 11 | 4 | gate sync 覆盖全部门禁且取值符合预期 | **sync** + 人工 |
| 12 | 5 | mem 守卫路径（sources_extra.cmake）仍匹配 | 人工 |
| 13 | 5 | mem 守卫在 target_config.h 的取值符合预期 | 人工 |
| 14 | 6 | application.bin 字节级 sanity | 人工 |

点 4–8 现已配置期强制；点 11 由 gate sync 自动执行、取值需人工确认；其余仍为人工。
