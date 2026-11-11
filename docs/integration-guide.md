# liteos_m 源码级集成指南

在任何下游 CMake 工程**从源码**编译 liteos_m 内核——使用集成方自己的工具链与编译选项。无需 prebuilt 归档、无需 objcopy 后处理、构建期不依赖 GN/Ninja。

ABI 选项（`-march`/`-mabi`/`--target`/...）由集成方**继承**，因此内核与固件其余部分始终保持同一 ABI——这是源码级集成的核心思想。

## 快速开始

```cmake
# --- 你的板子必须提供 -------------------------------------------
set(EVOLVE_BOARD_DIR     /path/to/your/board)   # 见"Board 契约"

# --- 一行集成 ---------------------------------------------------
add_subdirectory(kernel/liteos_m)

# --- 链接 -------------------------------------------------------
# musl 从源码编译时（LOSCFG_LIBC_MUSL=1），两个库都要链：
target_link_libraries(your_fw PRIVATE liteos_m_kernel liteos_m_musl)
```

工具链/ABI 选项（`-march`/`-mabi`/`--target`/`CHIP_<NAME>=1`）由你的
toolchain 文件或 `CMAKE_C_FLAGS` 提供——内核不添加自己的（见"已知限制"）。

## 提供的目标

两种集成路径，按宿主构建系统自动选择：

| 路径 | 目标 | 用途 |
|---|---|---|
| 纯 CMake（无 `build_component()` 基础设施） | `liteos_m_kernel` | STATIC——内核 + 板级胶水源码 |
| | `liteos_m_kernel_headers` | INTERFACE——仅公共头（供只需头文件的消费方） |
| 两条路径都创建 | `liteos_m_musl` | STATIC——musl 移植 + `lib/posix` 胶水（`LOSCFG_LIBC_MUSL=1` 时） |
| fbb 式宿主（定义了 `build_component()` 且 `TARGET_COMPONENT` 命中 `liteos_m`） | `liteos_m`（+ `liteos_m_interface`） | 组件化路径，编进宿主的组件体系 |

**链接注意**：不要对 `liteos_m_musl` 使用 `--whole-archive`——其
`stdio/remove.o` 与 fork VFS 的全局 `remove()` 冲突（fbb 实测教训，完整的
lazy 提取/位置无关分析见 `src/kernel/liteos_m_port/CMakeLists.txt` 尾部注释）。
section 收集的自注册代码若确需 whole-archive，只对 `liteos_m_kernel` 用。

## 功能开关：target_config.h 是唯一事实源

`LOSCFG_*` 功能开关**只在板头文件**
`${EVOLVE_BOARD_DIR}/include/target_config.h` 里改。配置期 gate sync
（`build/cmake/target_config_sync.cmake`）解析 `sources*.cmake`（自动
快照 + 手写契约）的全部模块门禁，并按 `target_config.h` 重置每一个：

| target_config.h | 模块源码 | 编译进代码的宏 |
|---|---|---|
| `#define LOSCFG_X 1` | **编入** | X=1 |
| `#define LOSCFG_X 0` | 剔除 | X=0 |
| 未定义 | 剔除 | los_config.h `#ifndef` 默认（OFF） |

即**源码选择永远跟随编译宏**——改 `target_config.h` 立即生效，无需重新导出
快照。优先级：`EVOLVE_MODULE_DISABLES`（集成方强制关闭）> `target_config.h`
> 快照。没有强制*启用*的变量：那是本机制要防的漂移（源码在、宏不在，或反之）。

`kconfig_values.cmake` 快照退化为：数值型 `LOSCFG_*`（限额/周期/栈大小）的
GN 基线，供分叉守护对比（见下）。布尔开关不再从快照取值。

## 集成契约

所有变量在 `add_subdirectory(kernel/liteos_m)` **之前**设置。
每个 `EVOLVE_*` 变量都有**复现 3322/hispark_hiditing 参考实现的默认值**——
现有 fbb 构建不设任何变量即逐字节一致。只覆盖你的板子不同的部分。

### 路径与平台

| 变量 | 必需 | 默认值 | 含义 |
|---|---|---|---|
| `EVOLVE_BOARD_DIR` | ✔ | `../liteos_m_port` | Board 目录（见"Board 契约"） |
| `EVOLVE_TOP_DIR` |  | *(本仓)* | liteos_m 源码根目录 |
| `EVOLVE_SOURCE_ROOT` |  | 本仓父目录 | 托管 `third_party/` 依赖的目录 |
| `EVOLVE_MUSL_DIR` |  | `${EVOLVE_SOURCE_ROOT}/third_party/musl` | musl 仓 checkout 位置（独立仓，可任意放） |

### 源码选择（板级专属源、musl 补源与排除项）

| 变量 | 默认值（3322） | 含义 |
|---|---|---|
| `EVOLVE_EXTRA_SOURCES` | `los_trap.S`、`riscv_lcmp_clic.c`、`dw_apb_timer.c` | `export_cmake_sources.py` 采集不到的板级专属源（脚本只遍历 `//kernel/liteos_m:libkernel` GN 图；板级 trap/驱动源在其外）。追加到内核源码列表。 |
| `EVOLVE_EXCLUDE_SOURCES` | *空* | 硬排除锤子——无守卫可表达的需求（如与 prebuilt 重复的副本）。3322 的 memstat/slab 剔除已由 `sources_extra.cmake` 的 mem 子树守卫自动完成（`target_config.h` 沉默 → gate sync → OFF）。空列表保留全部。 |
| `EVOLVE_MUSL_EXTRA_SOURCES` | vprintf + sprintf/fprintf/ferror/setbuf/qsort/qsort_nr/strpbrk/strcasestr/difftime/mbrlen/mbsrtowcs/mbsnrtowcs/wcsnrtombs + riscv32 setjmp/longjmp | musl link-gap 补源：porting 树缺的 vprintf + 上游 `src/` 全树里快照从不遍历的文件（每文件成因见 `build/cmake/sources_extra.cmake` 注释）。换 musl checkout 或换架构（非 riscv32 必须去掉 setjmp 对）时覆盖。 |

### 模块禁用与分叉守护

| 变量 | 默认值（3322） | 含义 |
|---|---|---|
| `EVOLVE_MODULE_DISABLES` | *空* | 强制置 `"0"` 的 LOSCFG 符号——优先级最高的关停锤，胜过 target_config.h。3322 已不需要（SIGNAL/PLATFORM_EXC 在 target_config.h 中未定义，gate sync 自动剔除）。空列表 = 完全交给 target_config.h。 |
| `EVOLVE_DIVERGE_GUARD` | `LOSCFG_BASE_CORE_TICK_PER_SECOND`、`LOSCFG_BASE_CORE_TIMESLICE_TIMEOUT`、`LOSCFG_HWI_PRIO_LIMIT`、`LOSCFG_CPUP_SAMPLE_PERIOD`、`LOSCFG_BASE_IPC_QUEUE_LIMIT` | `target_config.h` **必须**覆盖 GN 基线的关键值（否则静默 runtime 漂移）。配置期缺失则 `FATAL_ERROR`。空列表关闭守护——仅当你的板子有意匹配基线时使用。 |

### 符号与头文件配置

| 变量 | 默认值（3322） | 含义 |
|---|---|---|
| `EVOLVE_LIBC_INCLUDE_DIRS` | porting musl include | libc 头目录（musl）。其他板指向自己的 libc 树；空则省略。 |
| `EVOLVE_LWIP_INCLUDE_DIRS` | fbb lwip 布局（`${ROOT_DIR}/ohos/...`；无 `ROOT_DIR` 的纯 CMake 工程默认为空） | lwip 头目录。空则省略。 |
| `EVOLVE_SECUREC_INCLUDE_DIR` | fbb middleware `hwsec_c/include` | `securec.h` 位置（musl 目标的 `lib/posix` 源包含它）。跨出 kernel 仓边界，故可注入；musl 开启且文件缺失时配置期 WARNING。 |
| `EVOLVE_PUBLIC_DEFINES` | lwip 配置宏（`LWIP_CONFIG_FILE`/`LWIP_SHARED_CONFIG_FILE`） | 所有内核 TU 的额外 `-D` 宏（kernel/headers/musl 三类目标消费）。整表覆盖；空则省略。 |

### Board 契约（`EVOLVE_BOARD_DIR`）

```
<board_dir>/                      # 即 EVOLVE_BOARD_DIR（参考 src/kernel/liteos_m_port/）
├── kernel_configs/
│   └── debug.config              # GN/Kconfig 参考片段——本集成路径不读取，
│                                 # 仅供 GN 导出流程参考；功能开关在 target_config.h
├── include/                      # 首位 include 路径；gate sync 与 los_config.h
│   ├── target_config.h           # 消费的 target_config.h **必须**在此
│   ├── lm_asm_config.h           # ASM -include 用（汇编安全的最小配置）
│   └── soc.h                     # 及其它板级公共头
└── board/
    └── asm/                      # hal/寄存器头（含 3322 的 soc.h/interrupt_config.h 等）
```

> **路径敏感**：`target_config.h` 必须位于 `<board_dir>/include/`——
> gate sync（`build/cmake/target_config_sync.cmake`）与分叉守护都按
> `${EVOLVE_BOARD_DIR}/include/target_config.h` 查找；放错位置只会得到
> 一条 WARNING 并静默退化到冻结快照门禁。起步模板
> `build/cmake/board_template/` 即本布局，直接复制即可。

完整示例见 hs-fbb 集成：`src/kernel/liteos_m_port/`（板级胶水独立成
`liteos_m_board` 组件，与内核组件解耦）。

## 符号重复解决

当固件已提供内核也定义的符号（如不同的 libc、你自己的 `gettimeofday`），按情况选择：

* **剔除内核副本**——`EVOLVE_EXCLUDE_SOURCES`：
  ```cmake
  set(EVOLVE_EXCLUDE_SOURCES
      ${EVOLVE_SOURCE_ROOT}/third_party/musl/porting/liteos_m/kernel/src/stdio/remove.c)
  ```
  （evolve 的 VFS 已提供 `remove`；musl 副本被剔除。）

## 源码列表来源

`build/cmake/sources.cmake` 与 `build/cmake/kconfig_values.cmake` /
`menuconfig.snapshot.h` 是维护者从 GN 构建图生成的**已提交导出**
（见 `tools/export_cmake_sources.py`）。下游构建绝不调用 GN。
手写契约（板级 gap-fill、musl 补源、mem 子树守卫）在伴生的
`build/cmake/sources_extra.cmake`——重导出只覆盖 `sources.cmake`，
手工内容绝不能并入自动导出。

由于 board 的 `.config` 决定图中包含哪些模块，导出也快照了
*已展开*的 Kconfig 状态。若大幅改动 board 配置（开关整个子系统），
请让维护者重新导出——或自行运行：

```bash
# 1. 用你的 board 参数 gn gen（GN 来自 OHOS prebuilts）
gn gen /tmp/out --args='device_path="//path/to/board/liteos_m" \
                     product_path="//path/to/board" \
                     device_company="yourcompany" \
                     ohos_kernel_type="liteos_m"'
# 2. 导出源码 + Kconfig 快照
python3 tools/export_cmake_sources.py --gn-out /tmp/out
# 3. 提交 build/cmake/{sources,kconfig_values}.cmake + menuconfig.snapshot.h
```

> **注意：** 导出脚本会 shell out 调用 `python`（OHOS 的
> `check_linux_cpu.py` 构建钩子需要它）。若系统只有 `python3`，
> 创建一个 shim：`ln -sf $(which python3) /tmp/opencode/bin/python`，
> 并把该目录放到 `PATH` 最前面。

导出中的模块组由其 `LOSCFG_*` 开关守护，小幅配置改动经 target_config.h
的 gate sync 在 CMake 期生效，无需重新导出。只有**新增/删除整个模块组**
（快照里没有的源文件组）才需要重新导出。

## 已知限制

* 内核以 `-fno-builtin` 等选项编译（其自身语义），但**不**加
  `-fshort-enums`/`-mabi`/`-march`——枚举/ABI 布局跟随集成方。
  确保跨边界共享的头不依赖短枚举（hs-fbb 集成已为 riscv32 验证）。
* musl 源码编译由 `LOSCFG_LIBC_MUSL` 门控（3322 开启，源码编译替代
  prebuilt `libc.a`）：porting 树 + `lib/posix` 胶水 + `EVOLVE_MUSL_EXTRA_SOURCES`
  补源一起编进独立的 `liteos_m_musl` 库（`features.h` 的 `#define weak`
  会劫持内核 TU 的 `__attribute__((weak))`，故必须隔离 include 域）。
  想用 prebuilt libc 的板子：在 `target_config.h` 不定义
  `LOSCFG_LIBC_MUSL`，并经 CCFLAGS / `EVOLVE_LIBC_INCLUDE_DIRS` 提供
  libc 头路径。
* 内核的 `menuconfig.h` 来自 GN genconfig 快照（`-imacros`），*不是*
  重新解析 `.config` 文本——纯文本解析看不到 Kconfig `select` 派生符号。
  **但本集成路径不注入 `menuconfig.h`**：生效配置是 `target_config.h`
  （经 `los_config.h:40`），分叉守护 + gate sync 都围绕它。

## 3322 参考实现（默认值）

所有 `EVOLVE_*` 变量默认复现 hs-fbb / 3322 / hispark_hiditing 集成。
该板是同一 SoC（`3322` = `hispark_hiditing`）。默认值汇总：

```
EVOLVE_EXTRA_SOURCES       = los_trap.S + riscv_lcmp_clic.c + dw_apb_timer.c
EVOLVE_EXCLUDE_SOURCES     = 空（memstat/slab 经 sources_extra.cmake 守卫自动剔除）
EVOLVE_MUSL_EXTRA_SOURCES  = vprintf + 13 个上游 src/ 文件 + riscv32 setjmp 对
EVOLVE_MODULE_DISABLES     = 空（gate sync 按 target_config.h 自动处理）
EVOLVE_DIVERGE_GUARD       = TICK + TIMESLICE + HWI_PRIO + CPUP + QUEUE_LIMIT
EVOLVE_LIBC_INCLUDE_DIRS   = porting musl include
EVOLVE_LWIP_INCLUDE_DIRS   = ${ROOT_DIR}/ohos/third_party/lwip/...
EVOLVE_SECUREC_INCLUDE_DIR = fbb middleware hwsec_c/include
EVOLVE_PUBLIC_DEFINES      = LWIP_CONFIG_FILE + LWIP_SHARED_CONFIG_FILE
```
（`CHIP_<NAME>=1` 不在此列——它由集成方 CCFLAGS 提供，内核不注入。）

* 板级胶水：`src/kernel/liteos_m_port/`（独立 `liteos_m_board` 组件）
* 开关：`python3 build.py diting-community-kernel-lm`（hs-fbb 仓 src/ 下）

## 移植新板

1. **创建 board 目录**，遵循上面的 Board 契约（复制 `build/cmake/board_template/`
   作为起点并编辑）。
2. **在 `target_config.h` 里开/关你的特性**——gate sync 保证源码选择跟随。
3. **设置你的板不同的 `EVOLVE_*` 变量**（多数板只需 `EVOLVE_BOARD_DIR`；
   换 musl checkout 用 `EVOLVE_MUSL_DIR`，换架构记得覆盖
   `EVOLVE_MUSL_EXTRA_SOURCES` 去掉 setjmp 对）。
4. **若新增/删除整个模块组**，重新运行导出（见上），使 `sources.cmake`
   反映你的模块集。
5. **构建并**对照基线验证大小/符号。
