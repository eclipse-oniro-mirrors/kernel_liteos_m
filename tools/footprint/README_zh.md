# footprint - LiteOS-M 内核 RAM 占用统计工具

> English version: [README.md](README.md)

**版本**: v1.6
给定标准构建产物，自动产出**该源码版本、该平台配置**下的内核 RAM 占用
报告（Markdown + Excel 多页签）。纯标准库实现，零第三方依赖，仅需 Python 3.6+；
编译探针可选使用目标交叉工具链（gcc + nm，优先复用构建产物元数据中记录的
实际编译器）。

- **静态部分**（数据段/RAM 驻留代码/栈）：以链接 map 为唯一真值，符号级可追溯
- **动态部分**（控制块数组等运行期申请）：按当前源码的分配公式现算，
  **模块开关与数量上限完全由平台配置驱动**——没开的模块不参与计算
- **内存池**：堆池（g_memStart 数组 / `.heap` 段）为共享基础设施不计入
  RAM，"内存池布局"节展示载体形态

## 1. 环境要求

| 项 | 要求 | 说明 |
|---|---|---|
| Python | 3.6+ | 纯标准库 |
| 内核源码树 | **工具目录须整体放在内核源码树内**（如 `tools/footprint/`，目录上溯两级即内核根） | 动态公式靠活检索内核源码；放错位置动态部分会全部告警失效 |
| 构建输出目录 | 含 `*.map` 和 `config.h` | 标准 OHOS 构建输出目录天然满足 |
| 交叉工具链 | 自动发现：构建输出目录 `toolchain.ninja` 记录的编译器 → PATH → 源树 `device/soc`/`prebuilts` 搜索（另需 `nm`） | 用于编译探针实测 `sizeof`；需要探针却找不到编译器时直接报错 |

## 2. 快速上手（一条命令）

```bash
python3 footprint.py --out <构建输出目录>
# 例: python3 footprint.py --out out/arm_mps2_an386/qemu_mini_system_demo
```

运行后先看**输入回显**，确认工具选中的 map 与 config 就是想要的：

```
inputs: map=/path/to/OHOS_Image.map
        config=/path/to/config.h (auto-converted from config.h)
probe: 17 values measured by /path/to/cross-gcc
RAM 构成: 静态符号 17117 + 栈 4096 + 动态申请 41120 (+ 开销 288); 堆池 0 B 不计入 (共享基础设施, 见内存池布局)
RAM 合计 (不含节点开销): 62333 B (60.87 KB)
RAM 合计 (含节点开销): 62621 B (61.15 KB)
report written to ./footprint_report/report.md + footprint.xlsx + summary.json
```

`RAM 构成` / `RAM 合计` 三行把大小总结直接打到终端（数字为示意），
明细见三个输出文件。

报告输出到**执行命令时所在目录**的 `footprint_report/` 下（`--output` 可改）。
单次运行 < 1 s（实测：an386 0.45 s、ws63t 0.63 s，含编译探针全链路）。

**重跑环比**：每次运行同时落盘 `summary.json`（机器可读总计 + 分组数字 +
输入溯源）。同输入重跑进同一 `--output` 目录时，自动打印与上一版的
delta 表，无需额外参数即可获知相邻两次构建的内存差异。输入不同（换平台/
换 map/config）则一行说明跳过对比，绝不跨构建误比。

## 3. 输入要求与发现规则

| 输入 | 发现规则 | 缺失/歧义 |
|---|---|---|
| map | 目录下唯一 `*.map`（先顶层后递归） | 无/多个 → 快速报错；多个时列出候选，改用 `--map` 指定 |
| config | 显式 `--config` → 顶层 `.config` → 唯一递归命中 → **`config.h` 自动转换** | 全部缺失 → 报错并说明标准目录要求 |

- `config.h`（OHOS 构建生成物）自动转换为宏环境：`1`→y、整数、字符串；
  未定义宏按未启用处理（fail-closed）。转换来源在报告头与告警区留痕，
  并提示"Kconfig `.config` 为事实源，建议复核"
- 报错原则：**缺输入快速失败、不做降级猜测**——降级等于错数据

## 4. 统计范围与规则

| 输入 | 作用 |
|---|---|
| 链接 map | 静态 RAM 真值（符号级） |
| config（`.config` 或 `config.h`） | 宏取值与模块开关 |
| 内核源码（工具所在源码树，固定） | 动态分配公式锚点检索 |
| 板级 `target_config.h` | 从 map 归属自动发现并合并（板级宏可能超出 config） |

- RAM 合计 = 静态符号 (`.data`+`.bss`) + 栈 + 动态申请；堆池为共享
  基础设施不计入；栈按 `.stack*` 前缀与 `*_stack` 后缀段全计
- `.data` 只计 RAM 侧一次
- 动态项按源码公式推算：`数量 × sizeof(结构体)`；尺寸宏与类型由编译探针
  实测，锚点可声明 `probe_headers` 引入模块私有头、`lhs` 限定目标分配语句
  （同一初始化函数内的兄弟分配不会被误合并）
- 节点开销 = 逐堆节点 `align_up(大小,8)−大小+节点头`；节点头按构建配置推导
  （TLSF：8+INTEGRITY_CHECK/LEAKCHECK/TASK_MEM_USED；BESTFIT：
  16+HEAD_BACKUP/LEAKCHECK；如 an386=16B、ws63t=32B，单独列出）

**纳入范围**（三层漏斗，漏斗外绝不静默丢弃）：

1. 归属匹配内核库：`libliteos_m.a(` 或路径含 `kernel/liteos_m`
2. 成员前缀属于 12 个内核组：base / arch / common / cpup / backtrace /
   exchook / signal / interrupt / hpl / trace / lms / power（posix、cmsis、osal 等
   不在内核组内，进"排除项"并注明原因）
3. 输出段分类：data/bss/stacks/heap（含 RISC-V 小数据段 `.sdata/.sbss`
   与 `.got`；`.text_sram/.text_tcm` 等 RAM 驻留代码计入 RAM）

无法归类但归属内核库的成员（未知前缀，如新增内核模块）进
"未识别成员"警告桶，单列呈现，绝不静默丢弃。

## 5. 输出解读

`report.md` 逐节：

| 节 | 内容 |
|---|---|
| 统计说明 | 纳入/排除范围、计数规则 |
| 汇总 | 段类别 RAM 合计，RAM 构成公式 |
| 子模块明细 | 12 组 × 段交叉表（RAM 类） |
| 动态申请明细 | 每项大小/状态/公式/取值来源 |
| 动态项分类总览 | 按取值来源归类计数：源码扫描现算 / 登记公式（建议人工核验）/ map 真值 / 配置未启用 |
| 内存池布局 | sys 堆载体形态（内部数组/链接段/自由尾巴）、堆入口符号 |
| 段对账 | RAM 相关输出段：段头/归属/fill/合并-relax/差额（信任检查） |
| 排除项 / 告警 | 漏斗外内容与原因 / 运行期检查结果 |

`footprint.xlsx` 页签：汇总 / RAM符号 / 动态项 / 排除项 / 段对账
（/未识别成员，有才出现）。`summary.json` 伴生文件（总计+分组+输入溯源）
供重跑环比与 CI 消费。

**动态项状态含义**：

| 状态 | 含义 | 计入合计？ |
|---|---|---|
| `dynamic` | 源码为 `LOS_MemAlloc`，公式按本平台配置现算 | 是 |
| `disabled` | 配置门未开（模块未启用） | 否（尺寸列显示 `-`，列于未启用项） |
| `map-static` | 符号已在 map（静态数组形态） | 已在静态合计，不重复加 |
| `source-static` | 源码是静态数组但 map 没有 | 版本错配警告 |
| `unknown` | 源码形态变化，锚点失效 | 否，告警提示更新锚点 |

## 6. 运行期自检（进告警区）

- **锚点自检**：本次构建内核源码中活跃的 `g_* = LOS_MemAlloc(...)` 若无
  锚点覆盖则告警——新增动态分配点不会静默漏计；成员路径左值（如
  `g_runtimeLogger.compressBuf`）同样检测，且须由登记该项的锚点精确覆盖
  （同函数其它锚点不掩蔽）；扫描范围含未识别桶成员
  （新代码族先落未识别桶，其开机分配同样必须暴露，2026-10-27 HPL 前车之鉴）
- **段对账**：输出段头 vs 归属记录 vs fill 差额超阈值告警（字符串合并/relax
  已按地址并集去重）
- 源码或 config 比 map 新 → 版本错配警告

## 7. 模块开关如何影响结果

动态项存在性由平台配置驱动（锚点 `when` 门 + 源码 `#if` 预处理双保险），
换平台/裁剪配置后重跑即可，无需改工具。示例（an386）：

| 平台配置 | 工具行为 |
|---|---|
| RWSEM=y, LIMIT=6 | g_allRwsem = 6 × 16 = **96 B** 计入动态 |
| 未配置 RWSEM | g_allRwsem → disabled，不计算 |
| DEBUG_MUTEX=y 且 MUX=y, LIMIT=24 | g_muxDebugArray = 24 × 16 = **384 B** |
| 只开 DEBUG、模块关 | disabled（组合门缺一不可） |

## 8. CLI 选项

| 选项 | 说明 |
|---|---|
| `--out <dir>` | 构建输出目录（客户标准入口，自动发现 map/config） |
| `--map <file>` + `--config <file>` | 全显式模式（CI / 非标准目录） |
| `--output <dir>` | 报告目录（默认 `./footprint_report`） |
| `--cc <cc>` | 显式指定交叉编译器（默认自动发现：构建元数据 → PATH → 源树搜索） |
| `--sizeof-table <json>` | 预测值表 `{'sz:TYPE'/'mac:NAME': 整数}`，完全替代编译探针——归档构建离线复现，无需工具链 |

退出码：`0` = 报告已产出（若有告警，见报告告警区）；`1` = 致命错误，无报告。

## 9. 目录结构

```
tools/footprint/
├── footprint.py    # 入口: CLI 解析、输入发现、公共管线 (run_pipeline)
└── src/
    ├── input.py    # 输入解析: .config/config.h/板级合并、GNU map、#if 预处理、公式求值器
    ├── anchors.py  # 动态申请: 锚点表、源码活检索、编译探针、求值引擎
    ├── scope.py    # 范围与汇总: 段分类、三层漏斗、堆池剔除、分组、段对账
    └── output.py   # 报告输出: Markdown + Excel (纯标准库 zip+XML)
```

依赖单向：`input ← anchors ← scope ← output`。

## 10. 常见问题

**Q: 报错 "no config found ... (neither Kconfig .config nor generated config.h)"？**
目录里既无 `.config` 也无 `config.h`，不是标准构建输出目录。确认传的是
`out/<平台>/<组件>` 这层目录，或改用 `--map` + `--config` 全显式。

**Q: config.h 自动转换的结果可信吗？**
转换语义与 Kconfig 一致且 fail-closed；风险在来源（生成物可能含构建流程
注入的宏）。报告告警区会提示复核；有 Kconfig `.config` 放同目录自动优先。

**Q: 告警 "内核动态分配未入锚点表"？**
内核出现了锚点表未覆盖的持久分配点。把报告反馈给维护者，或在 `src/anchors.py` 的
锚点表（`DEFAULT_ANCHORS`）补充条目。

**Q: 没有交叉工具链？**
动态项需要探针却找不到编译器时工具会直接报错（不会出缺数的合计）。在有构建环境
的机器上运行——工具会自动从构建元数据（`toolchain.ninja`）与源树定位编译器；
特殊环境可用 `--cc` 显式指定。归档构建也可传预测值表（`--sizeof-table`），
完全旁路探针。

**Q: 想调整纳入/排除范围？**
编辑 `src/scope.py` 的范围表（`DEFAULT_SCOPE`），或把需求反馈给维护者。

## 11. 局限

- 动态公式锚点随源码版本演进，与历史人工基线可能存在已文档化的统计差
- 锚点自检仅可见文件域全局及其成员路径的直赋（`g_x = ...`、
  `g_x.member = ...`）；局部变量、出参、辅助函数内的分配
  （如任务栈经创建通用管道）对其不可见，依赖锚点登记覆盖
- 间接分配（内核经外壳/组件申请的内存）不在锚点表内
- AllocAlign 类分配的对齐垫（0~7B/项）随池地址浮动，静态不可知不计
- 产品调用接口触发的分配（LOS_QueueCreate/LOS_TaskHplSizeSet 等）为
  B 类触发型，属产品侧占用，不计入内核合计（报告不再罗列）
- 多核场景按单核处理
- 无 function-sections 的构建，静态明细粒度为输入段
