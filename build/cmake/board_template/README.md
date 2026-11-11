# board_template —— 将 liteos_m 集成到新产品的起点

复制本目录作为你的 board 胶水目录并填入占位值。它是 `EVOLVE_BOARD_DIR`
契约期望的最小骨架（见 `docs/integration-guide.md`）。

## 目录结构

```
board_template/                  # 复制本目录作为你的 board 胶水目录
├── kernel_configs/
│   └── debug.config              # LOSCFG_* Kconfig 片段——开启你的特性
├── include/                      # Board 契约的 ${EVOLVE_BOARD_DIR}/include
│   ├── target_config.h           # 生效的 C 层配置（时序、堆、限额）
│   ├── lm_asm_config.h           # 汇编安全的最小配置（ASM -include）
│   └── soc.h.example             # 最小 SoC 头示例（复制为 soc.h 并填值）
└── board/                        # Board 契约的 ${EVOLVE_BOARD_DIR}/board
    └── asm/                      # 你的架构专属 asm 头（hal_*.h）
```

## 步骤

1. `cp -r build/cmake/board_template <your_board_dir>`（放在本仓外，或你的产品存放 board 胶水的地方）。
2. 编辑 `include/target_config.h`——**这是功能开关的唯一事实源**：
   时钟、tick、堆、任务/IPC 限额，以及各 `LOSCFG_*` 模块开关
   （`#define LOSCFG_X 1` 编入 / `0` 或不定义剔除）。配置期 gate sync
   会据此自动增删 `sources*.cmake` 里对应模块的源码。
   `kernel_configs/debug.config` 只是 GN/Kconfig 参考片段，本集成路径
   **不读取**——留着供 GN 重新导出快照时参考。
3. 编辑 `include/target_config.h` 中的分叉守护值——分叉
   守护检查的 5 个值（TICK/TIMESLICE/HWI_PRIO/CPUP/QUEUE_LIMIT）
   **必须**在此定义，除非你将 `EVOLVE_DIVERGE_GUARD` 置空。
4. 编辑 `include/lm_asm_config.h`——镜像栈/IRQ/**FPU/架构**值。
   **关键**: `LOSCFG_ARCH_FPU_DOUBLE` 必须与 CCFLAGS 的 `-march *d` 配对,
   否则 C/ASM TaskContext 布局分裂 → 运行时栈损坏;`LOSCFG_ARCH_LINX_M`
   (LinxCore131/170) 必须镜像 target_config.h,否则 mcause 掩码退化 →
   中断 load access fault。详见文件内注释与 `docs/integration-guide.md`。
5. `cp include/soc.h.example include/soc.h` 并填入你的 SoC 值（若你的 SoC
   已自带 soc.h 则删掉示例）；hal/寄存器头放入 `board/asm/`。
6. riscv 板还需提供 **`riscv_hal.h`**——内核 `arch/risc-v/src/los_interrupt.c`
   直接 `#include` 它（中断控制器的读寄存器/Hart 上下文接口）。3322 参考：
   `src/kernel/liteos_m_port/board/riscv_hal.h`（+ 同目录 `riscv_hal.c`
   板级胶水源，经你的宿主组件或 `EVOLVE_EXTRA_SOURCES` 编入）。
7. 在你产品的 CMakeLists.txt 中，`add_subdirectory(kernel/liteos_m)` 之前：

   ```cmake
   set(EVOLVE_BOARD_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/path/to/your_board_dir)
   # 可选：覆盖 3322 默认值：
   # set(EVOLVE_EXTRA_SOURCES    ...)   # 你的 trap/驱动源
   # set(EVOLVE_MUSL_EXTRA_SOURCES ...) # 换 musl checkout/架构时覆盖
   # set(EVOLVE_MODULE_DISABLES  ...)   # 强制关闭的 LOSCFG_*（胜过 target_config.h）
   # set(EVOLVE_DIVERGE_GUARD    ...)   # 你的关键符号，或置空
   ```
   工具链/ABI 选项（`-march`/`-mabi`/`--target`/`CHIP_<NAME>=1`）由你的
   toolchain 文件或 `CMAKE_C_FLAGS` 提供，内核不注入。
8. 构建并对照基线验证大小/符号。

## 参考

3322/hispark_hiditing 集成是参考实现——所有 `EVOLVE_*` 变量默认为其值，
因此现有 fbb 构建不设任何变量即逐字节一致。完整示例见
`vendor/hispark_hiditing/kernel_configs/` 与
`targets/hispark_hiditing/liteos_m/board/target_config.h`。
