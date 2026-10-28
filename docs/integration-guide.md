# liteos_m 源码级集成指南

在任何下游 CMake 工程**从源码**编译 liteos_m 内核——使用集成方自己的工具链与编译选项。无需 prebuilt 归档、无需 objcopy 后处理、构建期不依赖 GN/Ninja。

ABI 选项（`-march`/`-mabi`/`--target`/...）由集成方**继承**，因此内核与固件其余部分始终保持同一 ABI——这是源码级集成的核心思想。

## 快速开始

```cmake
# --- 你的板子必须提供 -------------------------------------------
set(EVOLVE_BOARD_DIR     /path/to/your/board)   # 见"Board 契约"
set(EVOLVE_EXTRA_COMPILE_OPTIONS ${YOUR_CCFLAGS})  # 你的工具链/ABI 选项
# 注: CHIP_<NAME>=1 define 由你的 CCFLAGS 提供(名字归集成方定),
#     内核不注入, 避免与集成方自己的 define 撞 -Wmacro-redefined.

# --- 一行集成 ---------------------------------------------------
add_subdirectory(kernel/liteos_m)

# --- 链接 -------------------------------------------------------
target_link_libraries(your_fw PRIVATE
    -Wl,--whole-archive liteos_m_kernel -Wl,--no-whole-archive)
# whole-archive：拉入 section 收集的自注册代码
```

提供的目标：

| 目标                     | 用途                                       |
|--------------------------|-------------------------------------------|
| `liteos_m_kernel`        | STATIC 库——全部内核 + musl 移植源码（+ 你的扩展） |
| `liteos_m_kernel_headers`| INTERFACE——仅内核公共头（供仅需头文件的消费方） |

## 集成契约

所有变量在 `add_subdirectory(kernel/liteos_m)` **之前**设置。
每个 `EVOLVE_*` 变量都有**复现 3322/hispark_hiditing 参考实现的默认值**——
现有 fbb 构建不设任何变量即逐字节一致。只覆盖你的板子不同的部分。

### 路径与平台

| 变量 | 必需 | 默认值 | 含义 |
|---|---|---|---|
| `EVOLVE_BOARD_DIR` | ✔ | `../liteos_m_port` | Board 目录（见"Board 契约"） |
| `EVOLVE_EXTRA_COMPILE_OPTIONS` | ✔ | *(无)* | 集成方工具链/ABI 选项（CCFLAGS）。内核不添加自己的；`CHIP_<NAME>=1` 也由此提供。 |
| `EVOLVE_TOP_DIR` |  | *(本仓)* | liteos_m 源码根目录 |
| `EVOLVE_SOURCE_ROOT` |  | 本仓父目录 | 托管 `third_party/` 依赖的目录 |
| `EVOLVE_BUILD_TYPE` |  | `debug` | 选择 `kernel_configs/<type>.config` |

### 源码选择（板级专属源与排除项）

| 变量 | 默认值（3322） | 含义 |
|---|---|---|
| `EVOLVE_EXTRA_SOURCES` | `los_trap.S`、`riscv_lcmp_clic.c`、`dw_apb_timer.c` | `export_cmake_sources.py` 采集不到的板级专属源（脚本只遍历 `//kernel/liteos_m:libkernel` GN 图；板级 trap/驱动源在其外）。追加到内核源码列表。 |
| `EVOLVE_EXCLUDE_SOURCES` | `los_memstat.c`、`los_slab.c`、`los_slabmem.c` | 要完全剔除的源——其 `LOSCFG_*` 守护在 `target_config.h` 中缺失，或与 prebuilt 重复。从源码列表移除。空列表保留全部。 |

### 模块禁用与分叉守护

| 变量 | 默认值（3322） | 含义 |
|---|---|---|
| `EVOLVE_MODULE_DISABLES` | `LOSCFG_POSIX_API`、`LOSCFG_FS_VFS`、`LOSCFG_KERNEL_SIGNAL`、`LOSCFG_PLATFORM_EXC` | 强制置 `"0"` 的 LOSCFG 符号。POSIX/VFS/signal/exchook 与 prebuilt `libc.a` 重复或 3322 不用。空列表不禁用任何模块。 |
| `EVOLVE_DIVERGE_GUARD` | `LOSCFG_BASE_CORE_TICK_PER_SECOND`、`LOSCFG_BASE_CORE_TIMESLICE_TIMEOUT`、`LOSCFG_HWI_PRIO_LIMIT`、`LOSCFG_CPUP_SAMPLE_PERIOD` | `target_config.h` **必须**覆盖 GN 基线的关键值（否则静默 runtime 漂移）。配置期缺失则 `FATAL_ERROR`。空列表关闭守护——仅当你的板子有意匹配基线时使用。 |

### 符号与头文件配置

| 变量 | 默认值（3322） | 含义 |
|---|---|---|
| `EVOLVE_LIBC_INCLUDE_DIRS` | fbb musl 布局 `${ROOT_DIR}/open_source/musl/...` | libc 头目录（musl）。其他板指向自己的 libc 树；空则省略（如 prebuilt libc 由 CCFLAGS 提供头路径）。 |
| `EVOLVE_LWIP_INCLUDE_DIRS` | fbb lwip 布局 `${ROOT_DIR}/ohos/third_party/lwip/...` | lwip 头目录。空则省略。 |
| `EVOLVE_PUBLIC_DEFINES` | lwip 配置宏 | 所有内核 TU 的额外 `-D` 宏。 |
| `EVOLVE_EXCLUDE_SOURCES` | *(见上)* | 亦用于按文件剔除解决符号重复（见下）。 |

### Board 契约（`EVOLVE_BOARD_DIR`）

```
<board_dir>/
├── kernel_configs/
│   └── debug.config          # LOSCFG_* Kconfig 片段（特性裁剪）
└── liteos_m/
    └── board/                # target_config.h, hal 头, ...
        ├── include/
        └── asm/
```

完整示例见 hs-fbb 集成：`src/kernel/liteos/evolve_boards/hi3322/`。

## 符号重复解决

当固件已提供内核也定义的符号（如不同的 libc、你自己的 `gettimeofday`），按情况选择：

* **剔除内核副本**——`EVOLVE_EXCLUDE_SOURCES`：
  ```cmake
  set(EVOLVE_EXCLUDE_SOURCES
      ${EVOLVE_SOURCE_ROOT}/third_party/musl/porting/liteos_m/kernel/src/stdio/remove.c)
  ```
  （evolve 的 VFS 已提供 `remove`；musl 副本被剔除。）

  注意：设置 `EVOLVE_EXCLUDE_SOURCES` 会**覆盖** 3322 默认值
  （memstat/slab 排除项）。若要扩展而非替换，从默认列表起头再追加。

## 源码列表来源

`build/cmake/sources.cmake` 与 `build/cmake/kconfig_values.cmake` /
`menuconfig.snapshot.h` 是维护者从 GN 构建图生成的**已提交导出**
（见 `tools/export_cmake_sources.py`）。下游构建绝不调用 GN。

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

导出中的模块组由其 `LOSCFG_*` 开关守护，因此小幅配置改动在 CMake
期生效，无需重新导出。

## 已知限制

* 内核以 `-fno-builtin` 等选项编译（其自身语义），但**不**加
  `-fshort-enums`/`-mabi`/`-march`——枚举/ABI 布局跟随集成方。
  确保跨边界共享的头不依赖短枚举（hs-fbb 集成已为 riscv32 验证）。
* musl 移植源**默认禁用**（在 `sources.cmake` 中以 `if(FALSE)` 包裹），
  因为 fbb 产品使用 prebuilt `libc.a`。已提交的 `menuconfig.snapshot.h`
  仅为参考快照——**不**经 `-imacros` 注入。若你的板子需要源码编译
  musl，移除 `sources.cmake` 中的 `if(FALSE)` 包裹，并确保 libc 头路径
  经 `EVOLVE_LIBC_INCLUDE_DIRS`（或 CCFLAGS）提供。
* 内核的 `menuconfig.h` 来自 GN genconfig 快照（`-imacros`），*不是*
  重新解析 `.config` 文本——纯文本解析看不到 Kconfig `select` 派生符号。

## 3322 参考实现（默认值）

所有 `EVOLVE_*` 变量默认复现 hs-fbb / 3322 / hispark_hiditing 集成。
该板是同一 SoC（`3322` = `hispark_hiditing`）。默认值汇总：

```
EVOLVE_EXTRA_SOURCES       = los_trap.S + riscv_lcmp_clic.c + dw_apb_timer.c
EVOLVE_EXCLUDE_SOURCES     = los_memstat.c + los_slab.c + los_slabmem.c
EVOLVE_MODULE_DISABLES     = POSIX_API + FS_VFS + KERNEL_SIGNAL + PLATFORM_EXC
EVOLVE_DIVERGE_GUARD       = TICK + TIMESLICE + HWI_PRIO + CPUP
EVOLVE_LIBC_INCLUDE_DIRS   = ${ROOT_DIR}/open_source/musl/{include,arch/riscv32,arch/generic}
EVOLVE_LWIP_INCLUDE_DIRS   = ${ROOT_DIR}/ohos/third_party/lwip/...
```
（`CHIP_<NAME>=1` 不在此列——它由集成方 CCFLAGS 提供，内核不注入。）

* 包装器：`src/kernel/liteos/liteos_m_integrate.cmake`
* board：`src/kernel/liteos/evolve_boards/hi3322/`
* 开关：`USE_EVOLVE_KERNEL=1 python3 build.py 3322-recovery`

## 移植新板

1. **创建 board 目录**，遵循上面的 Board 契约（复制 `build/cmake/board_template/`
   作为起点并编辑）。
2. **设置你的板不同的 `EVOLVE_*` 变量**（多数板只需 `EVOLVE_BOARD_DIR` +
   `${EVOLVE_EXTRA_COMPILE_OPTIONS}`；其余继承 3322 默认值）。
3. **若大幅改动 Kconfig**，重新运行导出（见上），使 `sources.cmake`
   反映你的模块集。
4. **构建并**对照基线验证大小/符号。
