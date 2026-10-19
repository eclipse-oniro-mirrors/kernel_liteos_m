# SiteClaw 与 LiteCrab 嵌入式 Agent 对比分析

> 对比日期：2026-09-29<br>
> SiteClaw 来源：[GDD_MOSI/SiteClaw](https://gitcode.com/GDD_MOSI/SiteClaw)，分析快照 `e96df93943971beeb5f18c77b04d5c0fb6a8bbc1`（2026-08-05）<br>
> LiteCrab 来源：当前仓库 `feature/progressive-kconfig-architecture` 分支，分析快照 `366167fe5c11daaff98af354af2e537d99ab4eb7`（2026-09-29）

## 1. 结论先行

SiteClaw 和 LiteCrab 都实现了 ReAct、模型调用、工具调用、会话和记忆，但产品定位并不相同：

- **SiteClaw 是通用 Agent 平台**：重点是多 Agent、多前端、多 Channel、异步并发、VFS/知识检索、通用定时任务、工具沙箱和动态扩展。
- **LiteCrab 是设备产品级 Agent 运行时**：重点是 C11、明确的板端边界、Kconfig 编译裁剪、静态库嵌入、可注入 LLM/设备工具、告警诊断以及 SD5091/Hi3516 产品适配。
- 如果目标是 **SD5091/Hi3516 上把 Agent 作为现有设备软件的一部分交付**，LiteCrab 当前路线更直接，产物边界和能力裁剪也更可审计。
- 如果目标是 **Linux/Android 上运行多 Agent 服务，并同时提供 TUI、WebUI、HTTP、WebSocket、知识库和通用任务调度**，SiteClaw 的功能面更完整。
- 目前不能基于仓库材料认定 SiteClaw 在内存上一定优于 LiteCrab，也不能认定 LiteCrab 在端到端响应时间上一定更快。两边缺少同一硬件、同一模型、同一请求、同一功能集合下的统一基准。

从工程规模看，SiteClaw 的能力更广，但依赖、运行时和维护复杂度也明显更高；LiteCrab 的能力面较窄，但更接近“可嵌入、可裁剪、可按产品验收”的设备软件形态。

## 2. 对比口径与证据等级

本文将证据分为三类：

| 等级 | 含义 | 本文示例 |
|---|---|---|
| 实测 | 本次在当前环境实际构建、测试或统计 | LiteCrab Release 产物尺寸、28/28 CTest、源码规模 |
| 源码确认 | 可从当前提交的代码和构建声明直接确认 | SiteClaw workspace crate、Cargo feature、Tokio/SQLite 依赖、LiteCrab Capability Manifest |
| 项目声明 | README 或设计文档给出的目标/数据，本次未复现 | SiteClaw 1.5 MB 空闲内存、5–6 MB 业务内存、RTOS 支持、性能目标 |

### 2.1 本次实际执行环境

LiteCrab 使用以下命令构建并验证：

```sh
cmake -S . -B build-comparison \
  -DCMAKE_BUILD_TYPE=Release \
  -DLITECRAB_PROFILE=diagnostic_sd5091
cmake --build build-comparison -j2
ctest --test-dir build-comparison --output-on-failure
size build-comparison/litecrab_server
```

环境为 WSL x86-64、GCC 11.4.0，产物动态链接宿主机的 OpenSSL、libcurl 和 libc。它是 **Host 对比基线**，不是 SD5091 或 Hi3516 的最终板端数据。

当前 Windows/WSL 环境没有安装 Rust/Cargo，因此本次没有编译 SiteClaw。SiteClaw 的架构和功能结论来自源码；其内存数字来自项目 README，不能与 LiteCrab Host 数据直接相减或计算倍数。

## 3. 定位与适用场景

| 维度 | SiteClaw | LiteCrab |
|---|---|---|
| 核心定位 | 通用、多入口、多 Agent 的 Agent 平台 | 面向设备产品的可裁剪 Agent 内核/服务 |
| 主要语言 | Rust 2024 edition | C11，生成器使用 Python |
| 主要部署形态 | Rust library、TUI、独立 Gateway、WebUI 配套 | POSIX 可执行程序或 `liblitecrab_agent.a` 静态库 |
| 当前典型平台 | Linux、Windows、macOS、Android；README 声明 RTOS | POSIX/SD5091；Hi3516 静态库包装已完成 Host 验证 |
| 产品组织方式 | Cargo feature + 运行时 JSON Agent 配置 | Kconfig + Product defconfig + Capability Manifest + 运行时 JSON |
| 更适合 | 通用服务、交互前端、复杂编排、多个 Agent | 设备诊断、告警、机器人控制、已有 C/C++ 固件集成 |

一个重要区别是：SiteClaw 将“嵌入式”更多理解为在 Linux/Android/潜在 RTOS 上运行一个完整 Agent 平台；LiteCrab 将“嵌入式”进一步落实为 **主程序可不属于 Agent、Agent 以 C API/静态库嵌入、设备能力通过回调注入、无关能力不进入目标文件**。

## 4. 架构对比

### 4.1 SiteClaw

SiteClaw 是 15 个 workspace member 组成的 Rust 工程，主要链路如下：

```text
TUI / WebUI / HTTP / WebSocket
              │
        DeployShield
              │
           Gateway
              │
           Runtime
       ┌──────┼────────┐
     Actors  Memory   TimedTask / SkillCraft
       │       │             │
       └──── VirtCtx / VFS ──┘
              │
 Model / Channel / Tool / Sandbox / Observability / Comms
```

关键特征：

- `Gateway` 统一管理 Channel、Session 路由和 Agent 生命周期。
- `Runtime` 通过 `Actor` trait 组织 Think、Act、Observe，支持按 Agent 建立独立 Actor Registry。
- `Runner` 可创建、删除和更新多个 Agent，并提供普通与流式执行接口。
- Channel 支持 InMemory、HTTP Server/Client、WebSocket Server/Client。
- VirtCtx 用 `context://` VFS 隔离上下文、技能、记忆与知识数据。
- Tool 层具备风险分级、安全链、审计和 SandboxRouter；沙箱后端覆盖 Linux 的 bubblewrap/firejail、Docker，以及 Windows Job Object。
- Runtime/Gateway 主要建立在 Tokio 异步运行时上，对并发会话和多个入口更友好。

这套架构的优势是扩展面大、职责边界多、对复杂 Agent 产品友好；代价是 crate、trait、异步运行时和配置层次较多，嵌入已有设备进程时需要引入更大的运行时边界。

### 4.2 LiteCrab

LiteCrab 当前采用“核心内核 + 编译期 Capability + Product + 平台适配”的结构：

```text
Product defconfig + product.yml + prompt.md
                    │
       build/capabilities.yml
                    │
       tools/kconfig/configure.py
                    │
  源文件清单 / autoconf.h / 生命周期注册表 / Prompt
                    │
       Capability Manager
   init → start → quiesce → stop → destroy
                    │
 Runtime / Hub / Session / Kernel / LLM / Tool
             ┌──────┴──────┐
    POSIX standalone    Embedded C API
    TCP + OpenSSL       liblitecrab_agent.a
```

关键特征：

- `Kconfig` 定义 Platform、Artifact 和 Capability symbol。
- `defconfigs/<product>.defconfig` 固化产品能力组合。
- `build/capabilities.yml` 是能力到源文件、依赖、平台约束和外部库的唯一映射。
- 生成器在 CMake 配置阶段解析依赖图，输出源码清单、`autoconf.h`、能力注册表、Product Prompt 和能力报告。
- Capability Manager 统一执行初始化、启动、静默、停止和销毁，并在启动失败时逆序回滚。
- POSIX 版由 `standalone_app.c` 组装为 `litecrab_server`；嵌入式版通过 `LiteCrabAgentCreate/ExecuteSync/Cancel/Destroy` 接入主程序。
- LLM Transport 和 External Tool Backend 都可注入；Hi3516 主程序可保留自己的 ASR/TTS/设备控制，只调用 Agent 核心。

LiteCrab 的 Capability 关闭后，生成器不会把对应 `.c` 文件加入编译，也不会把适配器加入生成注册表。因此，即使公共头文件仍保留某些类型或函数声明，只要启用的代码没有引用未提供的实现，链接器就不会要求这些符号；矩阵测试还会用 `nm` 拒绝 Alarm、MCP、Gateway 等被关闭模块的符号。

这套架构比 SiteClaw 简单，适合固定资源和固定职责的设备；但当前仍以单 Agent 内核为主，不具备 SiteClaw 那种完整的多 Agent/多入口编排平台。

### 4.3 架构差异总结

| 项目 | SiteClaw | LiteCrab | 影响 |
|---|---|---|---|
| 扩展单元 | Rust crate、trait、Actor、Cargo feature | C 模块、Capability Ops、Kconfig symbol | SiteClaw 更适合运行时多态；LiteCrab 更适合编译期裁剪 |
| Agent 实例 | Runner 管理多个 Agent，可动态创建/删除 | 当前以一个 Agent handle/运行时为主 | SiteClaw 多租户与多角色能力更强 |
| 调度模型 | Tokio async，Gateway 按 Chat 派生任务 | POSIX 线程/同步执行；嵌入 API 同一 handle 单请求占用 | SiteClaw 并发上限更容易扩展；LiteCrab 行为更直接 |
| 上下文抽象 | `context://` VFS、多层检索 | Session、History、Working Memory、文件持久化 | SiteClaw 知识与上下文体系更完整 |
| 嵌入边界 | Rust API/DeployShield，通常携带 Tokio/Gateway | 稳定 C API、静态库、Transport/Tool 回调 | LiteCrab 更容易进入既有 C/C++ 设备进程 |
| 生命周期 | Gateway/Runtime/Actor 生命周期 | 生成注册表 + Capability Manager 统一生命周期 | LiteCrab 的产品能力启停顺序更集中可审计 |
| 配置校验 | Cargo feature + 各模块 JSON 校验 | Kconfig 依赖图 + Manifest 严格校验 + JSON | LiteCrab 对非法产品组合的构建期阻断更强 |

## 5. 功能对比

| 功能 | SiteClaw | LiteCrab | 结论 |
|---|---|---|---|
| ReAct 循环 | 支持，Actor 化 Think/Act/Observe | 支持，Kernel 执行模型/工具循环 | 两者均具备核心 Agent 能力 |
| 多模型 | Qwen、MiniMax、DeepSeek、Kimi、OpenAI-compatible，支持 primary/fallback | OpenAI-compatible 配置与可注入 Transport，可选择配置中的 provider | SiteClaw 内建 provider 与 fallback 更丰富 |
| 多 Agent | 多 Agent profile、动态创建/删除、Subtask Spawn Actor | 当前无通用多 Agent 编排 | SiteClaw 明显更强 |
| 流式响应 | Runtime 和 Channel 支持 streaming | 当前嵌入 C API 为同步完整响应；TCP 路径也以完整回复为主 | SiteClaw 交互体验更完整 |
| 前端/入口 | TUI、WebUI、HTTP、WebSocket、InMemory | TCP line gateway、C API；无内建 TUI/WebUI | SiteClaw 更适合通用交互产品 |
| Session | Chat/Session 路由和会话文件 | 用户/Session 映射、持久化、恢复与配额 | 都支持；组织方式不同 |
| 短期记忆 | InMemory FIFO、Context Snapshot | History + Working Memory | 都支持 |
| 长期记忆/知识 | VFS、分层记忆、BM25/向量/混合检索、Extractor | 文件记忆和 Memory Tool，无向量知识库 | SiteClaw 功能更深 |
| Skill | VFS Skill、SkillCraft 生成/编辑/评估 | 扫描 SKILL.md、Skill Tool/脚本执行 | SiteClaw 有自动创作闭环；LiteCrab 更偏静态技能 |
| 本地工具 | 文件、命令、Web 等 Tool Manager | Workspace、Process、Memory、Skill 等 | 都支持基础工具 |
| 动态工具 | 动态用户工具库 | MCP Client、External Tool Backend | 扩展路线不同 |
| MCP | 发现到 Tool 类型枚举，但当前快照未发现完整 MCP 客户端链路 | 独立 MCP Client Capability，支持远端设备工具发现/调用 | LiteCrab 当前更完整 |
| 设备工具 | 通过通用 Tool/Sandbox 扩展 | Robot Tool + 外部 backend 回调，针对主控设备集成 | LiteCrab 的板端适配更直接 |
| 通用定时任务 | CRUD、调度、重试、恢复、Webhook | 无通用调度器 | SiteClaw 更强 |
| 告警诊断 | 无 LiteCrab 同等级的设备告警业务模块 | Alarm 轮询/订阅/通知与 Agent 联动 | LiteCrab 的行业场景更成熟 |
| TTS | 未发现统一 TTS 输出抽象 | `tts_output` 作为嵌入产品契约；实际播放由主工程提供 | LiteCrab 已预留设备链路，但能力本身无源文件 |
| Observability | 日志、指标、追踪 trait/registry | JSONL Trace、日志、生命周期观测 | SiteClaw 指标抽象更丰富；LiteCrab 更贴近现有设备日志 |
| 沙箱 | 多后端进程沙箱、风险路由与审计 | 路径约束、Shell 策略、请求上限和安全测试，无通用进程沙箱 | SiteClaw 隔离能力更强 |
| 产品裁剪 | Cargo feature，可按 crate/Actor 选择 | Kconfig/Manifest 直接裁剪源文件并检查符号 | LiteCrab 的产品级裁剪证据更完整 |

## 6. 性能与并发

### 6.1 能够确认的事实

| 维度 | SiteClaw | LiteCrab |
|---|---|---|
| 调度方式 | Tokio 异步 Runtime；Gateway 对 Chat 执行使用异步任务 | TCP Gateway 使用线程；嵌入 API 为同步调用 |
| 并发模型 | 面向多 Channel、多 Agent、多 Chat 并发 | Standalone 支持连接处理；同一 `LiteCrabAgent` handle 忙时返回 `LITECRAB_AGENT_BUSY` |
| 流式输出 | 有 `execute_streaming` 和流式 Channel 链路 | 当前公开嵌入 API 无 token/chunk 回调 |
| 性能观测 | `MetricsObservable` 记录模型和工具耗时等指标 | Trace/日志覆盖请求、工具和生命周期，尚无统一 benchmark 报告 |
| 限制策略 | TokenBudget、迭代限制、Channel/任务配置 | 请求大小、LLM body、最终文本、迭代、调用次数、deadline/cancel 等上限 |

### 6.2 预期表现，不等同于实测结论

- **端到端对话延迟**通常由远端 LLM 首包和总生成时间主导，C 与 Rust 框架本身的差异可能不是主要因素。
- **高并发吞吐**方面，SiteClaw 的 Tokio、流式接口和多 Agent Registry 更有优势；LiteCrab 当前嵌入 API 的单 handle 串行语义更适合设备侧可控负载。
- **启动路径和空闲 CPU**方面，LiteCrab 的模块数量和初始化链更短，理论上更容易做到低开销，但本次没有进行冷启动/空闲采样，不能给出数字结论。
- **确定性**方面，LiteCrab 的固定上限、同步 API、静态产品组合更容易估算最坏路径；SiteClaw 的异步任务、VFS、SQLite、多个 Channel 和更多后台服务需要更完整的资源治理。

### 6.3 SiteClaw 文档中的性能数据

SiteClaw README 给出以下项目声明：

| 指标 | SiteClaw 声明值 |
|---|---:|
| 空闲运行时框架底噪 | 约 1.5 MB |
| Agent 对话/上下文/工具业务运行 | 5–6 MB |
| Android 内存目标 | 小于 10 MB |
| 冷启动目标 | 小于 3 秒 |
| ReAct 单轮目标（不含 LLM） | 小于 100 ms |

当前快照没有发现可直接执行的 Criterion/dhat benchmark、结果文件或能说明硬件、feature、allocator、采样方法的报告；测试策略文档中 `dhat` 仍标记为“未引入”。因此，上述数字应视为项目口径，不能视为本次独立验证结果。

## 7. 体积与资源占用

### 7.1 LiteCrab 本次实测

`diagnostic_sd5091` 全功能 Host Release 结果：

| 项目 | 数值 | 说明 |
|---|---:|---|
| ELF 文件大小 | 315,392 B（约 308.0 KiB） | x86-64 PIE，动态链接，未 strip |
| `.text` 等只读/代码统计 | 253,845 B（约 247.9 KiB） | GNU `size` 的 text 列 |
| 初始化数据 | 22,392 B（约 21.9 KiB） | GNU `size` 的 data 列 |
| 零初始化数据 | 3,326,768 B（约 3.17 MiB） | GNU `size` 的 bss 列，不等于完整 RSS |
| text + data + bss | 3,603,005 B（约 3.44 MiB） | 仅 ELF section 口径 |
| `liblitecrab.a` | 422,756 B（约 412.8 KiB） | Host archive，不是最终链接后的板端固件增量 |

注意：315 KB 的 ELF 文件 **不包含动态库部署体积**。当前 Host 产物通过 libcurl/OpenSSL 间接加载 nghttp2、SSH、LDAP、GSSAPI、压缩和 TLS 等系统库。在设备根文件系统已经提供这些库时，增量可以只计算可执行文件；若需要全静态、自带全部依赖，则必须重新测量静态链接产物。

3.17 MiB 的 BSS 也不等于运行时峰值内存：它不包含堆、线程栈、动态库映射、TLS/HTTP 缓冲和 LLM 响应缓存。LiteCrab 当前尚缺 SD5091/Hi3516 的峰值 RSS/heap/线程栈实测。

### 7.2 SiteClaw 可确认的体积信号

本次不能给出 SiteClaw 可执行文件字节数，原因是环境中没有 Rust 工具链，且不同入口的依赖集合不同：根包是 library，实际可执行入口主要是 `tui` 或 `siteclaw-deployshield`。

从源码和依赖可确认：

- workspace 有 15 个正式 member；仓库中共有 28 个 `Cargo.toml`（含辅助项目）。
- `Cargo.lock` 包含 321 个 package 条目。
- 默认能力链包括 Runtime、VirtCtx、HTTP、Skill、Channel、Gateway 和 DeployShield。
- Runtime 当前直接依赖 Tokio、rusqlite（bundled SQLite）、dirs、regex 等；网络侧使用 reqwest/rustls、WebSocket 和 Axum 等组件。
- 当前仓库没有自定义 `[profile.release]`，即没有仓库级的 LTO、`panic=abort`、strip 或 size opt 配置。

这些事实说明默认 SiteClaw 产物包含的通用能力和依赖面明显大于 LiteCrab，但在没有实际编译之前，不能把依赖数量转换为二进制大小，也不能据此给出倍数。

### 7.3 源码规模（维护体积，不是运行时体积）

采用“生产路径、排除明显 tests 目录和 `_test.rs`”的近似统计：

| 项目 | 生产源文件 | 代码/注释总行数 | 源文件字节数 |
|---|---:|---:|---:|
| SiteClaw Rust（`src/` + `tui/`） | 375 | 60,001 | 2,367,707 |
| LiteCrab C/H（`src/` + `include/`） | 61 | 12,779 | 494,718 |

SiteClaw 生产源码行数约为 LiteCrab 的 4.7 倍。这反映的是功能广度和维护面，不代表执行速度或产物体积必然也是 4.7 倍。

## 8. “嵌入式支持”成熟度分析

### 8.1 SiteClaw

SiteClaw README 声明 Linux、Windows、macOS、Android、EulerOS 和 `thumbv7em-none-eabihf` RTOS 目标。但当前快照还存在以下需要验证的点：

- 根包的 `runtime-smol` 和 `runtime-tokio` feature 当前为空，不会自行切换依赖或源码。
- Runtime 直接依赖 Tokio、bundled SQLite、dirs 等 `std` 生态组件。
- OSAL 默认 feature 是 `std`，当前未见完整 `no_std` 构建证据。
- `model-llama`、`model-onnx` 是空 feature；多级 `memory-*` 中也包含占位/分阶段声明。
- 未提供本次可核验的 Cortex-M 编译日志、链接 map、板端启动记录或资源报告。

因此，SiteClaw 的 Linux/Android 形态可以按通用 Rust 应用理解；RTOS/Cortex-M 应视为设计目标或在建能力，不能仅凭 target 名称认定已达到量产可用。

### 8.2 LiteCrab

LiteCrab 的 POSIX Host 形态已有完整构建和回归；Hi3516 形态已有以下主仓证据：

- `robot_hi3516` Product 生成静态库和 GN 源清单。
- 嵌入 API、Hi3516 Transport 包装、Robot Tool 回调完成 Host 编译测试。
- Host 符号检查确认 Robot 产物不包含 Alarm、MCP 和 TCP Gateway。

但 LiteCrab 也不能宣称 Hi3516 已完成发布验收：当前仓库不包含目标设备主工程，尚未执行正式 GN/Ninja 集成、目标工具链链接、ASR→Agent→Tool→TTS 真机链路、峰值资源测量和 8/24/72 小时稳定性测试。

两者相比，LiteCrab 的嵌入 API 和产品裁剪边界更具体；SiteClaw 的平台覆盖声明更广。两边都还需要目标板证据才能完成“嵌入式量产”结论。

## 9. 构建裁剪机制对比

### 9.1 SiteClaw Cargo feature

优点：

- Rust/Cargo 原生机制，crate 和代码块都可使用 `cfg(feature=...)`。
- 可组合 Runtime Actor、Gateway、Sandbox、Memory 等模块。
- 编译器可消除未使用泛型/函数，并有强类型边界。

当前限制：

- 顶层 feature、子 crate feature 和实际依赖裁剪之间并非全部一一对应。
- 某些 feature 是别名或占位；仅看到名称不能认为对应能力已经完整实现或已经减小产物。
- Runtime 的一组较重依赖目前是无条件依赖，最小 feature 仍需实测。

### 9.2 LiteCrab Kconfig/Manifest

优点：

- Product defconfig 是可审查的发布配置。
- Manifest 明确列出每项能力的源文件、适配器、依赖、平台约束和外部库。
- 关闭能力会从编译源列表删除实现，而不只是运行时 `if`。
- 生成器拒绝未知 symbol、缺依赖、冲突、环、缺源文件和路径越界。
- 配置矩阵通过符号拒绝检查验证 Alarm/MCP/Gateway 确实未链接。

当前限制：

- 核心源文件仍是固定集合，尚未细分为更多可裁剪原子。
- `LiteCrabContext` 公共结构仍直接包含 Alarm/MCP/POSIX 线程相关字段，类型层耦合尚未完全消除。
- `tts_output` 当前是产品契约标记，没有自己的实现源文件。
- Python 生成器是构建前置条件，需要固定依赖和版本策略。

总体上，SiteClaw 的 feature 更适合 Rust 生态内的模块组合；LiteCrab 的 Manifest 更适合设备产品做“源文件/符号/产物”级审计。

## 10. 测试与质量保障

| 维度 | SiteClaw | LiteCrab |
|---|---|---|
| 仓库测试规模 | 源码中约 1,090 个 `#[test]`/`#[tokio::test]` 标注 | C 单元/契约测试 + Python E2E，共 28 个 CTest |
| 本次执行结果 | 未执行：环境无 Rust/Cargo | Release 全量 28/28 通过，总耗时约 103.86 秒 |
| 测试分层 | LLT、SDV、SIT，Mock 与真实模型分层 | 单元、公共 API、配置生成器、TCP/MCP/Alarm/Session/E2E、压力与部署契约 |
| 配置裁剪验证 | 有 feature 设计和测试策略，未在本次运行 | 五个非默认组合有构建和拒绝符号检查记录 |
| 性能基准 | 设计文档定义目标，未发现落地 benchmark 结果 | 有 Host section budget；没有统一延迟/RSS benchmark |

测试数量不能直接比较质量：SiteClaw 的单元测试面更大，但本次没有执行；LiteCrab 的 28 个 CTest 中包含多个内部含大量断言的测试程序和端到端场景，不能简单按“28 对 1,090”判断覆盖率。

## 11. 风险与短板

### 11.1 SiteClaw

1. **默认依赖面较大**：Tokio、reqwest/rustls、WebSocket、Axum、bundled SQLite、VFS/检索等提高了通用性，也提高了产物、内存和供应链治理成本。
2. **部分 feature 名称超前于实现**：RTOS runtime 切换、本地模型、部分 Memory feature 仍需按实际源码和构建结果验收。
3. **嵌入已有 C 设备主程序的边界较重**：需要处理 Rust ABI、Tokio 生命周期、线程和错误跨边界问题。
4. **配置与运行时层次多**：Gateway、Agent、Actor、Channel、VFS、Tool/Sandbox 均有配置和生命周期，部署治理要求较高。
5. **性能数字缺少复现材料**：目前不能审计 1.5 MB/5–6 MB 对应的完整条件。

### 11.2 LiteCrab

1. **通用平台能力不足**：没有原生多 Agent 编排、WebUI/TUI、WebSocket、通用定时任务和向量知识库。
2. **同一嵌入 handle 并发受限**：同步 API 简单可靠，但流式体验和高并发能力不足。
3. **资源测量未完成闭环**：当前有 Host section 基线，没有 SD5091/Hi3516 的 RSS、heap、栈、线程、FD、冷启动和峰值延迟报告。
4. **动态链接依赖可能掩盖部署体积**：Host ELF 很小，但 libcurl/OpenSSL 依赖链不能忽略；目标系统若未预装会显著增加镜像。
5. **Capability 类型层仍有耦合**：关闭实现后虽然不会链接符号，但公共 Context/头文件尚未完全平台无关。
6. **记忆和知识能力较基础**：尚无向量检索、分层 VFS、自动记忆抽取和 SkillCraft 闭环。

## 12. 选型建议

### 12.1 优先选择 LiteCrab 的情况

- Agent 必须进入已有 C/C++ 设备主进程。
- 产品由 SD5091、Hi3516 等固定平台和固定业务能力组成。
- 必须证明关闭的功能没有进入目标文件。
- 设备已有 ASR/TTS/控制接口，希望以回调方式接入 Agent。
- 告警诊断、MCP 设备工具和现场部署约束比通用 UI 更重要。
- 团队需要较小、可审计、生命周期明确的代码边界。

### 12.2 优先选择 SiteClaw 的情况

- 同一服务需要运行多个 Agent、多个角色和多个会话。
- 需要 TUI、WebUI、HTTP、WebSocket 等多入口。
- 需要流式输出、通用定时任务、VFS、知识检索或复杂长期记忆。
- 需要进程沙箱、风险路由和动态工具库。
- 目标平台是资源相对充足的 Linux/Android，而不是 MCU 级 `no_std` 环境。

### 12.3 不建议的做法

- 不建议仅因 SiteClaw README 写有“RTOS”就直接替换 LiteCrab 的 Hi3516 嵌入路线。
- 不建议仅看 LiteCrab 315 KB Host ELF 就宣称完整部署体积小于 SiteClaw；动态库和 BSS 必须计入。
- 不建议把 SiteClaw 全部通用模块移植到 LiteCrab。应按实际产品需求选择性吸收设计，而不是复制平台规模。

## 13. LiteCrab 可借鉴的设计

按收益/成本排序，建议考虑：

1. **先建立统一性能基准**：固定模型 Mock、请求、工具链和并发数，测冷启动、首响应、纯框架单轮、RSS、heap 峰值、线程栈、FD 和产物尺寸。
2. **增加可选流式 C API**：在不破坏同步 API 的前提下增加 chunk callback 和 backpressure/cancel 约定。
3. **把 Agent 实例和 Session 调度进一步解耦**：需要多任务时可建立多个 handle 或受限实例池，而不是直接引入完整多 Agent 平台。
4. **完善 Tool 风险分级与审计链**：吸收 SiteClaw 的 SecurityChain/SandboxRouter 思路，但优先适配设备可用的 seccomp、权限用户或受控 helper。
5. **为知识检索定义轻量接口**：先抽象 `KnowledgeBackend`，让 SQLite/BM25/远端检索可按 Product 插拔，不把数据库强制加入核心。
6. **继续收紧 Capability 类型边界**：将 Alarm/MCP/POSIX 线程字段移出通用 Context，使关闭能力不仅不链接实现，也不污染公共类型。
7. **补齐真实目标板门禁**：SD5091 和 Hi3516 分别输出 map、`size`、RSS/heap、线程/FD、启动耗时、压力测试和稳定性报告。

## 14. 建议的统一对比基准

要形成可用于选型的最终性能结论，建议在同一台 aarch64 Linux 设备上构建以下两个等价组合：

```text
Case A：最小对话
- 单 Agent
- 同一 OpenAI-compatible Mock Server
- 无 Skill、无知识库、无定时任务、无告警
- 20 轮固定输入，固定 2 次工具调用

Case B：设备工具
- 单 Agent
- 同一模型响应脚本
- 3 个本地工具 + 1 个失败工具
- 100 次请求，1/5/10 并发

Case C：完整产品
- SiteClaw 默认 Gateway 业务组合
- LiteCrab diagnostic_sd5091 组合
- 分别报告，不宣称功能等价
```

每个 Case 至少记录：

| 类别 | 指标 |
|---|---|
| 产物 | 主二进制、必须随包发布的动态库、配置/资源文件、压缩包总大小 |
| 内存 | 启动后 RSS/PSS、空闲稳定值、单请求峰值、10 并发峰值、heap 峰值、线程栈预算 |
| 时延 | 冷启动、纯框架单轮、首 token/首 chunk、工具调度、完整响应 p50/p95/p99 |
| 吞吐 | 1/5/10 并发成功率、QPS、超时与取消响应时间 |
| 稳定性 | 8/24/72 小时 RSS 漂移、FD/线程泄漏、错误恢复 |
| 裁剪 | 关闭每项能力后的二进制差值和拒绝符号检查 |

只有完成这一基准，才能回答“哪个更快、哪个更省内存、哪个最终包更小”。

## 15. 最终判断

SiteClaw 的优势是功能广度、异步并发、多 Agent、多入口、VFS/知识和安全沙箱；LiteCrab 的优势是设备集成边界、C ABI、编译期裁剪、产品配置、生命周期审计和既有行业能力。

对当前 LiteCrab 项目而言，**不应把 SiteClaw 视为直接替代品，更适合把它作为通用 Agent 平台设计参考**。近期最有价值的工作不是追齐 SiteClaw 的所有功能，而是完成目标板资源门禁、流式接口、工具安全链和可复现 benchmark，使 LiteCrab 的“小、稳、可嵌入、可裁剪”优势从架构设计变成可以持续验证的数据。

## 16. 主要证据位置

### SiteClaw 快照

- `README.md`：定位、运行入口、平台声明、feature 和内存声明。
- `Cargo.toml`：15 个 workspace member、默认 feature 和核心依赖。
- `src/runtime/Cargo.toml`：Runtime Actor feature 及 Tokio/SQLite 等依赖。
- `src/runtime/src/runner/runner.rs`：Agent 创建/删除、执行和流式执行。
- `src/gateway/src/router/handlers/chat_handler.rs`：Chat 异步派发。
- `src/channel/`：InMemory/HTTP/WebSocket Channel。
- `src/virtctx/`：VFS、BM25、向量和混合检索。
- `src/tool/`、`src/sandbox/`：工具安全链、审计和沙箱后端。
- `docs/siteclaw-architecture-design.md`：整体架构与模块职责。
- `docs/siteclaw-test-strategy.md`：性能目标及 `dhat` 当前状态。

### LiteCrab 当前仓库

- [`README.md`](../../README.md)：Kconfig 架构使用指南和 Product 构建方式。
- [`build/capabilities.yml`](../../build/capabilities.yml)：能力源文件、依赖和平台约束。
- [`include/litecrab/agent.h`](../../include/litecrab/agent.h)：嵌入式 C API。
- [`include/litecrab/capability.h`](../../include/litecrab/capability.h)：Capability 生命周期接口。
- [`src/app/standalone_app.c`](../../src/app/standalone_app.c)：独立服务组装入口。
- [`src/app/embedded_agent.c`](../../src/app/embedded_agent.c)：嵌入式 Agent 实现。
- [`tests/config_matrix/matrix.yml`](../../tests/config_matrix/matrix.yml)：裁剪矩阵。
- [`tests/config_matrix/budgets.yml`](../../tests/config_matrix/budgets.yml)：Host 基线与待补目标板预算。
- [`docs/kconfig/litecrab_kconfig_implementation/IMPLEMENTATION_STATUS.md`](../kconfig/litecrab_kconfig_implementation/IMPLEMENTATION_STATUS.md)：实施与验证状态。
