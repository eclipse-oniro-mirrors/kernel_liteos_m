# board_template —— 将 liteos_m 集成到新产品的起点

复制本目录作为你的 board 胶水目录并填入占位值。它是 `EVOLVE_BOARD_DIR`
契约期望的最小骨架（见 `docs/integration-guide.md`）。

## 目录结构

```
board_template/
├── kernel_configs/
│   └── debug.config              # LOSCFG_* Kconfig 片段——开启你的特性
└── liteos_m/
    └── board/
        ├── include/
        │   ├── target_config.h   # 生效的 C 层配置（时序、堆、限额）
        │   └── lm_asm_config.h   # 汇编安全的最小配置（ASM -include）
        └── asm/                  # 你的架构专属 asm 头（hal_*.h）
```

## 步骤

1. `cp -r build/cmake/board_template <your_board_dir>`（放在本仓外，或你的产品存放 board 胶水的地方）。
2. 编辑 `kernel_configs/debug.config`——开启你的板需要的 `LOSCFG_*` 特性。
3. 编辑 `liteos_m/board/include/target_config.h`——设置时钟、tick、堆、
   任务/IPC 限额。分叉守护检查的 4 个值（TICK/TIMESLICE/HWI_PRIO/CPUP）
   **必须**在此定义，除非你将 `EVOLVE_DIVERGE_GUARD` 置空。
4. 编辑 `liteos_m/board/include/lm_asm_config.h`——镜像栈/IRQ/**FPU/架构**值。
   **关键**: `LOSCFG_ARCH_FPU_DOUBLE` 必须与 CCFLAGS 的 `-march *d` 配对,
   否则 C/ASM TaskContext 布局分裂 → 运行时栈损坏;`LOSCFG_ARCH_LINX_M`
   (LinxCore131/170) 必须镜像 target_config.h,否则 mcause 掩码退化 →
   中断 load access fault。详见文件内注释与 `docs/integration-guide.md`。
5. 将你的 `soc.h`、hal 头放入 `include/` 和 `asm/`。
6. 在你产品的 CMakeLists.txt 中，`add_subdirectory(kernel/liteos_m)` 之前：

   ```cmake
   set(EVOLVE_BOARD_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/path/to/your_board_dir)
   set(EVOLVE_EXTRA_COMPILE_OPTIONS ${YOUR_CCFLAGS})  # -march/-mabi/--target/CHIP_<NAME>=1 ...
   # 可选：覆盖 3322 默认值：
   # set(EVOLVE_EXTRA_SOURCES    ...)   # 你的 trap/驱动源
   # set(EVOLVE_MODULE_DISABLES  ...)   # 强制关闭的 LOSCFG_*
   # set(EVOLVE_DIVERGE_GUARD    ...)   # 你的关键符号，或置空
   ```
7. 构建并对照基线验证大小/符号。

## 参考

3322/hispark_hiditing 集成是参考实现——所有 `EVOLVE_*` 变量默认为其值，
因此现有 fbb 构建不设任何变量即逐字节一致。完整示例见
`vendor/hispark_hiditing/kernel_configs/` 与
`targets/hispark_hiditing/liteos_m/board/target_config.h`。
