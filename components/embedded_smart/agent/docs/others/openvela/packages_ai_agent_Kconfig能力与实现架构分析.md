# packages_ai_agent Kconfig 能力与实现架构分析

> LiteCrab 的单仓多场景落地方案见：[LiteCrab 单仓多场景 Kconfig 架构设计方案](LiteCrab单仓多场景Kconfig架构设计方案.md)

> 分析对象：`packages_ai_agent` 的 `dev` 分支  
> 代码快照：`4bcbd2a5e7f9724df77f7b17f1bb608321f8979c`  
> 分析日期：2026-09-28  
> 范围：`Kconfig`、`CMakeLists.txt`、`Make.defs`、`Makefile`、`include/agent_config.h` 以及各功能模块的条件编译和生命周期代码。

## 1. 结论先行

这个项目的 Kconfig 不是简单的“开关集合”，而是整个嵌入式 Agent 产品线的**编译期能力控制平面**。它同时控制：

1. AI Agent 是否作为 openvela/NuttX 应用进入构建；
2. 哪些源文件被编译进固件；
3. 哪些 API、工具、CLI 命令和生命周期分支可见；
4. 静态数组、连接池、线程栈等资源上限；
5. 设备身份、数据目录和 Shell 安全策略等产品参数。

其实现链路是：

```text
Kconfig 定义
   ↓ menuconfig / defconfig
NuttX .config
   ├──→ CMake/Make 条件：决定源文件和应用注册
   └──→ <nuttx/config.h> 中的 CONFIG_* 宏
              ├──→ #ifdef：裁剪 API、注册、初始化和分发路径
              ├──→ 宏别名：形成 AGENT_* 内部配置
              └──→ 数值参数：决定静态内存和线程参数
```

总体设计方向正确：它采用“**构建期粗粒度裁剪 + 源码期细粒度裁剪 + 运行时配置覆盖**”的三级模式，很适合 RAM/Flash 受限的 openvela 设备。

但当前实现存在明显的配置债务：

- 3 处 Kconfig 依赖符号拼写错误，使 7 个子配置不可见或无法正常设置；
- BLE 和测试开关引用不存在的源文件，启用后会构建失败；
- CMake 和 Makefile 的模块清单已经漂移；
- Voice、WebSocket、cron、heartbeat 等较大能力没有独立开关；
- 默认启用飞书、微信、MQTT、Node 和 MCP，主开关一开就不是最小配置；
- 若干参数名称看似控制全局，实际只影响主任务或局部线程；
- Kconfig 数据目录默认值与头文件 fallback、README 存在差异。

因此当前 Kconfig 可以支撑开发板和固定产品配置，但还没有形成“配置组合均可验证、依赖完整、资源可预测”的成熟产品线体系。

## 2. Kconfig 在 openvela/NuttX 中如何生效

NuttX 使用 Linux 风格 Kconfig。应用目录被纳入 apps/package 配置树后，`menuconfig` 展示本文件定义的 symbol；保存后写入板级 `.config`，构建系统再生成 C 代码可见的 `CONFIG_*` 宏。NuttX 官方文档说明，配置可通过 `menuconfig` 修改，也可以用配置片段合并；CMake 构建下可使用 `cmake --build <build-dir> -t menuconfig`。

本项目在该机制之上有四个落点。

### 2.1 应用注册层

顶层入口：

```kconfig
config EXAMPLES_AI_AGENT_VELA
    tristate "Vela AI Agent"
    default n
```

对应实现：

- `Make.defs`：只要 `CONFIG_EXAMPLES_AI_AGENT_VELA` 非空，就把本目录加入 `CONFIGURED_APPS`；
- `CMakeLists.txt`：用 `if(CONFIG_EXAMPLES_AI_AGENT_VELA)` 包住整个应用定义；
- `Makefile`：把该值赋给 `MODULE`。

这一步决定 AI Agent 是否存在于固件中。它是所有其他配置的父级，父级关闭时，内部配置整体不生效。

### 2.2 构建清单层

模块开关首先用于 CMake/Make 选择源文件。例如：

```cmake
if(CONFIG_AI_AGENT_FEISHU)
  list(APPEND AGENT_SRCS
    src/channels/feishu_bot.c
    src/channels/feishu_ws.c
    ...)
endif()
```

这是真正节省 Flash 的“粗裁剪”：源文件根本不会进入编译和链接。

### 2.3 C 预处理层

同一个开关还会包围头文件引用、初始化、启动、停止、消息分发和 CLI 命令：

```c
#ifdef CONFIG_AI_AGENT_WEIXIN
    weixin_channel_init();
    weixin_channel_start();
    weixin_channel_send(...);
    weixin_channel_stop();
#endif
```

这是“细裁剪”，用于保证调用方在模块未编译时也能通过编译，并移除对应控制流。

### 2.4 编译期数值层

数值和字符串 symbol 通过 `<nuttx/config.h>` 进入 `agent_config.h`，再映射为内部宏：

```c
#define AGENT_DATA_DIR CONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR
#define MCP_CLIENT_MAX_SERVERS CONFIG_AI_AGENT_MCP_MAX_SERVERS
#define CONN_POOL_SIZE CONFIG_AI_AGENT_TLS_CONN_POOL_SIZE
```

这类配置会直接改变静态数组大小、BSS 占用、线程入口参数和文件路径，不需要运行时读取。

## 3. 配置架构分层

```text
┌─────────────────────────────────────────────────────────────┐
│ 产品入口：EXAMPLES_AI_AGENT_VELA                           │
└──────────────────────────┬──────────────────────────────────┘
                           │
        ┌──────────────────┼───────────────────────┐
        ▼                  ▼                       ▼
  应用身份/资源       功能模块裁剪             安全策略
  名称、优先级、栈    Channel/Tool/Network      Shell choice
  数据目录、Node ID   UI/Camera/MCP/Node
        │                  │                       │
        └──────────────────┼───────────────────────┘
                           ▼
                    CMake/Make 源文件图
                           ▼
                    CONFIG_* 预处理图
                           ▼
          注册 → 初始化 → 启动 → 分发 → 停止/清理
                           ▼
                  运行时 config.json 覆盖
```

这是一种典型的软件产品线设计：Kconfig 决定“固件具有什么能力”，`config.json` 和 CLI 命令决定“已编译能力如何连接和运行”。两者边界原则上是：

- Kconfig：不可热变更，重新编译才生效；
- runtime config：API key、服务地址、模型、broker、token 等可部署时修改；
- 一个运行时配置不能唤醒未编译进固件的模块。

## 4. 33 个 Kconfig 项能力矩阵

### 4.1 应用入口、身份与资源参数

| 配置 | 默认值 | 实现位置 | 实际效果 | 评价 |
|---|---:|---|---|---|
| `EXAMPLES_AI_AGENT_VELA` | `n` | `Make.defs`、CMake、Makefile | 将应用加入 openvela/NuttX 构建 | 总开关有效 |
| `..._PROGNAME` | `ai_agent` | CMake/Makefile、MCP Server、A2A | NSH 命令名，同时成为 MCP/A2A 名称 | 多个语义耦合在同一参数 |
| `..._PRIORITY` | `100` | CMake/Makefile、小智线程 | 主应用优先级；小智线程复用该值 | 不控制 Agent/通道大多数线程 |
| `..._STACKSIZE` | `32768` | CMake/Makefile | 只控制应用主入口栈 | 不控制 Agent loop 等后台线程 |
| `..._DATA_DIR` | `/data/ai_agent` | `agent_config.h` | 派生 config/memory/session/skill 等全部路径 | 与 fallback `/data/agent` 及文档不一致 |
| `..._NODE_ID` | `watch-01` | `agent_config.h`、Node、小智 | 设备唯一标识和协议 ID | 有效，但出厂必须保证唯一 |
| `..._NODE_DISPLAY_NAME` | `Smart Watch` | Node client | Hub 展示名称 | 有效 |

`PROGNAME` 同时进入 `mcp_server.c` 和 `a2a_handler.c`，所以修改 NSH 命令名也会改变对外协议身份。更清晰的设计应拆为 `PROGNAME`、`SERVICE_NAME` 和 `A2A_AGENT_NAME`。

`PRIORITY` 和 `STACKSIZE` 的名称容易让人误以为它们控制整个 Agent。实际上 Agent loop 固定为 32KB/优先级 60，出站线程固定为 16KB/50，CLI 为 16KB/30，各通道也有自己的常量。这两个 Kconfig 只控制主应用任务，不能用于完整线程预算调优。

### 4.2 Shell 安全 choice

| 配置 | 模式 | 实现方式 |
|---|---|---|
| `..._SHELL_ALLOWLIST` | 生产默认 | `agent_config.h` 映射为安全级别 0；注册 `run_shell`，执行器只放行允许命令并阻止 shell 元字符 |
| `..._SHELL_FULL` | 开发 | 映射为级别 1；仍阻止关键系统命令，但允许管道/重定向等 |
| `..._SHELL_DENY` | 禁用 | 映射为级别 2；`tool_registry_init()` 不注册 `run_shell` |

这里同时使用 Kconfig `choice` 和 C 预处理，保证三种模式互斥。Deny 模式在能力发现阶段就删除工具，比仅在执行时返回拒绝更好。

### 4.3 接入通道

| 配置 | 默认 | 依赖 | 构建与运行效果 | 当前状态 |
|---|---:|---|---|---|
| `AI_AGENT_LVGL_UI` | `n` | `GRAPHICS_LVGL` | 加入 UI 源文件；增加 init/start/send/stop 和 `show_chat` CLI | CMake 可用；Makefile 路径错误 |
| `AI_AGENT_FEISHU` | `y` | 无显式依赖 | 加入 6 个 Bot/WS/HTTP 源文件；控制 Bot 生命周期和消息分发 | Bot 通道可裁剪；飞书文档/群聊工具仍始终编译 |
| `AI_AGENT_WEIXIN` | `y` | 无显式依赖 | 加入微信源文件；控制登录、轮询、发送和关闭 | 裁剪链完整 |
| `AI_AGENT_MQTT` | `y` | 无显式依赖 | 加入 MQTT channel；控制 init/start/send/stop | 功能可裁剪，但 CMake 仍无条件 `DEPENDS mqttc` |
| `AI_AGENT_XIAOZHI` | `n` | 无显式依赖 | 加入 WS、Opus、UDP；控制 CLI 和生命周期 | CMake 路径完整，Makefile未实现 |

关键边界：关闭 `AI_AGENT_FEISHU` 只关闭飞书 Bot 通道。`tool_feishu_doc.c` 和 `tool_feishu_chat.c` 仍在基础源文件清单里，相关工具仍会注册，依然可以通过 REST API 使用飞书能力。这个行为可以是有意设计，但配置名和帮助文本没有说明。

项目没有 `AI_AGENT_WEBSOCKET` 开关，`ws_server.c` 总是编译并在网络就绪后启动端口 28789。没有 Voice 总开关，ASR/TTS/采集/播放源文件也总是编译。因此“Kconfig 可裁剪全部通道”这一说法目前并不完全成立。

### 4.4 设备与媒体能力

| 配置 | 默认 | 依赖 | 实现 | 当前状态 |
|---|---:|---|---|---|
| `AI_AGENT_AUDIO_PREPROCESS` | `n` | `BES_AEC` | 引入 BES AEC 头；在录音流中创建 150KB NS+AGC heap 并逐块处理 | 有效 |
| `AI_AGENT_AUDIO_CAPTURE_GAIN` | `6`，范围 1–16 | 无 | 编译为采集增益；PCM 混音后乘法并裁剪 | 有效，且独立于预处理开关 |
| `AI_AGENT_CAMERA` | `n` | `VIDEO` | 加入 V4L2 camera 工具源文件并向工具注册表注册 `camera_capture` | CMake 有效；Makefile未实现 |
| `AI_AGENT_BLE_GATT` | `n` | `BLUETOOTH` | CMake 计划加入 `src/infra/ble_gatt.c` | 源文件不存在，启用即失败 |
| `AI_AGENT_BLE_NET` | `n` | `BLUETOOTH` | CMake 计划加入 `src/infra/ble_net.c` | 源文件不存在，启用即失败 |

音频预处理是比较完整的 Kconfig 设计：依赖硬件/库能力 `BES_AEC`，构建系统增加专用 include path，源码再通过 `#ifdef` 移除算法和 150KB heap 使用。

摄像头也具备“平台依赖 → 源文件 → 工具注册”完整链路。关闭时工具不会出现在 LLM capability list 中。

### 4.5 Agent 扩展能力

| 配置 | 默认 | 构建与运行效果 | 当前状态 |
|---|---:|---|---|
| `AI_AGENT_NODE` | `y` | 编译 Node client/manager；注册远程 tool provider；扩展上下文、WS 协议、飞书消息 fallback 和 CLI | 链路完整 |
| `AI_AGENT_MCP` | `y` | 编译 MCP Server/Client/Bridge/Registry；扩展工具 JSON、执行 fallback、CLI 和 cleanup | 链路完整 |
| `AI_AGENT_SKILL_SYNC` | `n` | 编译 `skill_sync.c`；网络就绪后在 Agent 启动前从飞书 Bitable 同步 | 主开关有效，子配置不可见 |
| `AI_AGENT_TEST` | `n` | 计划加入视觉集成测试并注册 `claw_test` | 测试源文件不存在，启用失败 |

Node 和 MCP 是实现最完整的两个编译期能力：它们不仅控制源文件，还控制跨模块 include、初始化顺序、工具 provider、动态 capability、CLI 和关闭清理。

### 4.6 网络与静态资源参数

| 配置 | 默认 | 实现 | 资源含义 | 当前状态 |
|---|---:|---|---|---|
| `AI_AGENT_NET_RPMSG` | `n` | `network_manager` 切换 RPMSG/TUN 路径；TLS 获取网络资源许可和动态超时；增加诊断 CLI、重连监听 | 高延迟 BLE/协处理器网络适配 | 有效，但依赖没有在 Kconfig 表达 |
| `AI_AGENT_MCP_MAX_SERVERS` | `4`，范围 1–8 | 决定 `s_servers[]` 静态数组 | 每槽约 456B BSS | 配置项被错误依赖隐藏，代码回退为 4 |
| `AI_AGENT_MCP_MAX_TOOLS` | `16`，范围 4–64 | 决定 `s_tools[]` 静态数组 | 每槽约 452B BSS | 配置项被错误依赖隐藏，代码回退为 16 |
| `AI_AGENT_TLS_CONN_POOL_SIZE` | `2`，范围 1–4 | 决定 TLS pool 静态数组和 raw buffer 数量 | 每槽约 12KB BSS | 有效 |

这类参数是嵌入式 Kconfig 最有价值的部分，因为它们让产品在编译期得到确定的内存上界。不过连接池大小还会间接改变 `TLS_RAW_BUF_COUNT`，实际增量并不只是单个结构体，应通过 map 文件和运行时 heap 水位重新量化。

### 4.7 Skill Sync 和测试参数

| 配置 | 预期效果 | 实际效果 |
|---|---|---|
| `AI_AGENT_TEST_IMAGE_DIR` | 指定视觉测试图片目录 | `depends on AGENT_TEST` 拼错，正常 menuconfig 不可见；头文件使用 fallback |
| `AI_AGENT_SKILL_SYNC_APP_TOKEN` | Bitable app token | 所在 `if AGENT_SKILL_SYNC` 拼错，正常不可见；代码得到空字符串 |
| `AI_AGENT_SKILL_SYNC_TABLE_ID` | Bitable table ID | 同上 |
| `AI_AGENT_SKILL_SYNC_MODE` | 增量/全量同步 | 同上，固定回退为 0（增量） |
| `AI_AGENT_SKILL_SYNC_DEVICE_TYPE` | 目标设备过滤 | 同上，固定回退为 `all` |

这些不是“文档问题”，而是功能可用性问题。即使启用 `AI_AGENT_SKILL_SYNC=y` 并编译成功，用户仍无法通过正常 menuconfig 设置 app token/table ID，启动同步会因空配置失败。

## 5. 从开关到功能的四种实现模式

### 5.1 模式 A：完整模块裁剪

代表：微信、MQTT、Node、MCP、LVGL、小智、Camera。

```text
Kconfig bool
  → CMake list(APPEND source)
  → 调用方 #ifdef include
  → init
  → start
  → dispatch/execute
  → stop/cleanup
```

这是最推荐的模式。一个配置的全部生命周期都在同一 symbol 下，可预测性最好。

### 5.2 模式 B：同一源文件内部算法裁剪

代表：`AI_AGENT_AUDIO_PREPROCESS`、`AI_AGENT_NET_RPMSG`。

源文件始终存在，但重型算法或另一套平台实现通过 `#ifdef` 替换。适合公共 API 不变、内部实现变化较大的模块。

### 5.3 模式 C：工具可见性裁剪

代表：Camera、Shell Deny。

开关不仅影响代码，还决定工具是否注册到 `tool_registry`。未注册工具不会进入 tools JSON 和 system prompt capability boundary，LLM 从能力发现阶段就看不到它。

### 5.4 模式 D：静态容量参数

代表：MCP Server/Tool 上限、TLS pool、音频增益。

Kconfig 数值直接进入数组维度和宏，生成不同的固件资源布局。优点是没有运行时分配的不确定性；缺点是每个配置组合都应该重新做 size/map 检查。

## 6. Kconfig 与运行时配置的边界

### 6.1 编译期 Kconfig

适合控制：

- 硬件依赖是否存在；
- 协议栈/大型模块是否进入固件；
- 最大连接数、工具数、栈和静态池；
- 量产不可变的安全基线；
- 产品 SKU 的能力组合。

### 6.2 运行时 `config.json`

项目通过 `config_store.c` 持久化：

- LLM host/model/API key；
- 搜索、飞书、微信和火山引擎凭证；
- MQTT broker/topic；
- OpenClaw gateway；
- 小智 endpoint；
- 工具 enable/disable 等。

因此编译期和运行期应理解为两层授权：

```text
Kconfig 编译进固件？
   └─否→运行时无论怎么配置都不可用
   └─是→runtime config 是否完整、工具是否 enabled？
             └─否→存在但不启动/拒绝调用
             └─是→能力可运行
```

当前并非所有模块都严格遵循第二层。例如默认编译的 WebSocket server 会在网络连通后直接启动，没有独立运行时 enabled 开关。

## 7. 当前默认配置画像

当用户只打开 `EXAMPLES_AI_AGENT_VELA=y` 而不调整子项时，默认同时启用：

- 飞书 Bot；
- 微信；
- MQTT；
- OpenClaw Node/Hub；
- MCP；
- Voice（无开关，始终编译）；
- WebSocket server（无开关，始终编译）；
- cron、heartbeat、Skills、视觉工具、文件/设备/媒体工具。

默认关闭：

- LVGL UI；
- BES 音频预处理；
- Camera；
- 测试；
- BLE GATT/NET；
- 小智；
- RPMSG/TUN 网络；
- Bitable Skill Sync。

所以当前默认更接近“功能演示版”，不是最小生产基线。对于手表级设备，建议采用显式产品 defconfig，而不是依赖这些默认值。

## 8. 配置依赖图

```text
EXAMPLES_AI_AGENT_VELA
├── identity: PROGNAME / NODE_ID / NODE_DISPLAY_NAME
├── process: PRIORITY / STACKSIZE / DATA_DIR
├── shell choice: ALLOWLIST | FULL | DENY
├── channels
│   ├── FEISHU
│   ├── WEIXIN
│   ├── MQTT
│   ├── LVGL_UI ──depends── GRAPHICS_LVGL
│   └── XIAOZHI
├── device/media
│   ├── AUDIO_PREPROCESS ──depends── BES_AEC
│   ├── AUDIO_CAPTURE_GAIN
│   ├── CAMERA ──depends── VIDEO
│   ├── BLE_GATT ──depends── BLUETOOTH
│   └── BLE_NET  ──depends── BLUETOOTH
├── agent extension
│   ├── NODE
│   ├── MCP
│   │   ├── MCP_MAX_SERVERS   [当前错误依赖 AGENT_MCP]
│   │   └── MCP_MAX_TOOLS     [当前错误依赖 AGENT_MCP]
│   ├── SKILL_SYNC
│   │   └── APP_TOKEN/TABLE_ID/MODE/DEVICE_TYPE
│   │       [当前错误父条件 AGENT_SKILL_SYNC]
│   └── TEST
│       └── TEST_IMAGE_DIR    [当前错误依赖 AGENT_TEST]
├── network: NET_RPMSG
└── resource: TLS_CONN_POOL_SIZE
```

## 9. 已确认的配置缺陷

### 9.1 P0：错误 symbol 导致配置项失效

当前写法：

```kconfig
config AI_AGENT_TEST_IMAGE_DIR
    depends on AGENT_TEST

if AGENT_SKILL_SYNC
    ...
endif

config AI_AGENT_MCP_MAX_SERVERS
    depends on AGENT_MCP

config AI_AGENT_MCP_MAX_TOOLS
    depends on AGENT_MCP
```

应该使用已定义的完整 symbol：

```kconfig
depends on AI_AGENT_TEST
if AI_AGENT_SKILL_SYNC
depends on AI_AGENT_MCP
```

Kconfig 会把未定义名称当作不可见的隐式 symbol，因此这些子配置在正常 menuconfig 中无法按预期出现。

### 9.2 P0：启用部分开关必然构建失败

- `AI_AGENT_BLE_GATT=y`：CMake 引用不存在的 `src/infra/ble_gatt.c`；
- `AI_AGENT_BLE_NET=y`：CMake 引用不存在的 `src/infra/ble_net.c`；
- `AI_AGENT_TEST=y`：CMake/Make 引用不存在的 `tests/integs/test_vision_integ.c`。

处理原则二选一：补齐实现和测试，或者暂时删除/隐藏开关，不能让 menuconfig 暴露不可构建组合。

### 9.3 P1：CMake 与 Makefile 漂移

Makefile 相比 CMake 缺少或错误处理：

- 缺 `src/core/arena_alloc.c`；
- 缺 `src/infra/url_parse.c`、`a2a_handler.c`；
- 缺 `src/tools/tool_amap.c`；
- 没有 Camera 和 XiaoZhi 条件源文件；
- 没有 BLE 条件源文件；
- LVGL 路径写成不存在的 `src/lvgl_ui/lvgl_ui_channel.c`，实际为 `src/ui/lvgl_ui_channel.c`；
- 音频预处理没有增加 BES AEC include path。

这意味着同一份 Kconfig 在两套构建系统下得到的能力集合不同。应指定 CMake 为唯一权威，或用一份源清单生成两套构建描述。

### 9.4 P1：依赖约束不足

顶层帮助文本写明需要网络和 mbedTLS，但 `EXAMPLES_AI_AGENT_VELA` 没有显式 `depends on`。飞书、微信、MCP、小智等也没有把网络、TLS、WebSocket/Opus 等必要平台能力完整表达在 Kconfig 中。

后果是用户能在 menuconfig 选择出“语义上允许、编译或运行时不成立”的组合。建议用实际 openvela/NuttX 平台 symbol 补足依赖，并用 `select` 时保持谨慎，避免强行打开底层能力造成反向依赖问题。

### 9.5 P1：数据目录定义不一致

- Kconfig 默认：`/data/ai_agent`；
- `agent_config.h` 未定义时 fallback：`/data/agent`；
- README/部分文档示例：`/data/agent`。

正常 Kconfig 构建会采用 `/data/ai_agent`，但仿真、独立编译或文档操作可能落到另一个目录，导致“配置存在但程序读不到”。应统一为一个路径，并提供存储迁移策略。

### 9.6 P1：默认组合偏重

FEISHU、WEIXIN、MQTT、NODE、MCP 都为 `default y`。根据仓库自身估算，仅这些模块活动时就可能消耗约 200KB 以上 RAM，尚未计入 Voice、TLS、LLM response 和媒体缓冲。

对于“主开关默认 n”的应用，子项默认 y 方便演示；对于多 SKU 量产，则建议：

- 通用默认全部 n；
- 用板级 defconfig/profile 明确开启；
- 提供 `MINIMAL`、`WATCH`、`SPEAKER`、`HUB` 等配置片段。

### 9.7 P2：可裁剪覆盖不完整

以下能力始终编译或启动，缺少 Kconfig：

- Voice ASR/TTS、采集、播放；
- WebSocket server；
- cron 和 heartbeat；
- vision；
- 飞书文档/群聊工具；
- media、health、QuickApp 等工具组；
- LLM Router 和 cache。

如果目标是 256KB 级 RAM 设备，应把这些能力按产品价值和依赖分组，而不是仅裁剪外围 channel。

## 10. 资源与二进制影响

### 10.1 Flash

由源文件是否加入 `AGENT_SRCS` 决定。模块级 bool 能显著减少代码段和只读数据；仅用运行时 enabled 开关不能节省 Flash。

### 10.2 BSS/静态 RAM

典型配置：

- MCP server slots：`MAX_SERVERS × 约456B`；
- MCP tool slots：`MAX_TOOLS × 约452B`；
- TLS pool：`1–4 × 约12KB`，另有 raw buffer；
- 各模块全局状态、ring buffer、连接状态。

### 10.3 Heap

Kconfig 会间接决定：

- 音频预处理约 150KB heap；
- 通道 TLS/响应缓冲；
- Node/MCP 动态 JSON 和远程工具描述；
- LVGL widget 和字体资源；
- Camera/vision 图片和 base64 缓冲。

### 10.4 Thread stack

当前只有主入口 stack 可配置，大部分后台任务栈硬编码在 `agent_config.h`。因此 menuconfig 不能完整反映线程 RAM 预算。建议把关键线程栈暴露成高级配置，或至少生成一张由构建配置计算出的静态 RAM 预算表。

## 11. 推荐的 Kconfig 重构

### 11.1 先修复正确性

```kconfig
config AI_AGENT_TEST_IMAGE_DIR
    depends on AI_AGENT_TEST

if AI_AGENT_SKILL_SYNC
    # APP_TOKEN / TABLE_ID / MODE / DEVICE_TYPE
endif

config AI_AGENT_MCP_MAX_SERVERS
    depends on AI_AGENT_MCP

config AI_AGENT_MCP_MAX_TOOLS
    depends on AI_AGENT_MCP
```

删除未实现 BLE/TEST 选项，或补齐源文件后再公开。

### 11.2 按能力域组织菜单

建议使用 `menu`/`menuconfig` 分组：

```text
Vela AI Agent
├── Core settings
├── Channels
├── Voice and multimedia
├── Device capabilities
├── Tool runtime
├── MCP and distributed nodes
├── Networking
├── Security
├── Memory sizing
└── Tests and diagnostics
```

目前 33 个 symbol 平铺在一个 `if` 下，后续扩展会迅速失去可读性。

### 11.3 增加核心裁剪开关

建议新增：

- `AI_AGENT_VOICE`；
- `AI_AGENT_WS_SERVER`；
- `AI_AGENT_CRON`；
- `AI_AGENT_HEARTBEAT`；
- `AI_AGENT_VISION`；
- `AI_AGENT_LLM_ROUTER`；
- `AI_AGENT_LLM_CACHE`；
- `AI_AGENT_TOOLS_FEISHU`、`..._MEDIA`、`..._HEALTH`、`..._QUICKAPP`。

每个开关都应覆盖：源文件、头文件、注册、init/start、dispatch、stop/cleanup、CLI、文档和测试。

### 11.4 配置 profile

建议用 defconfig fragment，而不是在 Kconfig 中堆叠复杂互选：

| Profile | 建议能力 |
|---|---|
| Minimal CLI | CLI + LLM + 文件/时间工具；关闭所有远程 channel、Node、MCP、Voice |
| Watch | Voice + LVGL + health/control；小连接池；可选微信 |
| Speaker | Voice + media + MQTT；关闭 LVGL/Camera |
| Hub | WebSocket + Node + MCP + 较大工具上限；关闭本地 UI |
| Developer | Full Shell + tests + trace；不得用于量产 |

NuttX 官方支持配置片段合并，适合维护这种产品矩阵。

### 11.5 建立“配置契约”

每个 Kconfig symbol 都应有机器可验证的契约：

```text
symbol
  ├── owner 模块
  ├── direct platform dependencies
  ├── selected source files
  ├── exported CONFIG_* / AGENT_* macros
  ├── lifecycle hooks
  ├── Flash/BSS/heap/thread cost
  ├── test configuration
  └── incompatible combinations
```

这可以由简单脚本从 manifest 生成文档和 CI matrix，避免 CMake/Makefile/文档三方漂移。

## 12. CI 配置矩阵建议

至少验证以下组合的 configure + compile + link：

1. `AI_AGENT=y`，全部可选模块关闭；
2. 默认配置；
3. 每个模块单独开启；
4. 所有可用模块开启；
5. Shell 三种 choice；
6. MCP 最小/最大容量；
7. TLS pool 1/4；
8. LVGL + Voice + Camera 的高内存组合；
9. RPMSG 网络组合；
10. CMake 和 Make 构建结果对比（如果决定继续维护 Makefile）。

CI 还应做静态检查：

- Kconfig 中 `if/depends on/select` 引用的内部 symbol 是否真实存在；
- CMake/Make 引用的源文件是否存在；
- 每个模块开关是否同时出现在构建清单和生命周期代码；
- 未受任何 Kconfig 控制的大型源文件列表；
- defconfig 中无效或已重命名 symbol；
- 固件 map 文件中的 Flash/BSS 预算是否越界。

## 13. 推荐实施顺序

### P0：让所有可见配置都正确

1. 修正 `AGENT_TEST`、`AGENT_SKILL_SYNC`、`AGENT_MCP` 三类错误引用。
2. 删除或补齐 BLE、测试缺失源文件。
3. 修复 Makefile 的 LVGL 路径及遗漏；若不再支持 Make，明确废弃。
4. 统一 `/data/ai_agent` 与 `/data/agent`。

### P1：让配置组合可验证

1. 增加 Kconfig lint 和源文件存在性检查。
2. 建立 minimal/default/full 配置矩阵。
3. 为每个模块补充启动、工具可见性和 CLI smoke test。
4. 用 map 文件记录每个 profile 的 Flash/BSS 差异。

### P2：让 Kconfig 成为产品架构

1. 按能力域重组菜单。
2. 增加 Voice、WS、cron、heartbeat、tool groups 开关。
3. 把关键线程栈和队列大小纳入高级配置。
4. 建立 Watch/Speaker/Hub 等产品 defconfig。
5. 从单一 manifest 生成构建清单、配置说明和资源预算。

## 14. 最终架构评价

该项目已经具备嵌入式软件产品线的雏形：Kconfig 决定能力边界，CMake 决定链接边界，`CONFIG_*` 决定代码边界，运行时 `config.json` 决定部署参数。Node、MCP、Camera、LVGL 等模块展示了正确的端到端裁剪方式。

目前的问题不是缺少开关，而是**配置契约没有闭环**：部分开关不能构建、部分依赖写错、两套构建清单不一致、部分大模块无法裁剪。优先修复这些一致性问题，再扩展更多配置项，才能真正获得 Kconfig 带来的确定资源、稳定组合和多产品复用价值。

## 附录 A：配置生效定位索引

- Kconfig 定义：`Kconfig`
- CMake 源文件选择：`CMakeLists.txt`
- Make 应用发现：`Make.defs`
- 传统 Make 源文件选择：`Makefile`
- 宏映射与内部默认：`include/agent_config.h`
- 生命周期条件：`src/agent_main.c`
- 工具可见性：`src/tools/tool_registry.c`
- Shell 策略：`src/tools/tool_shell.c`
- RPMSG 网络切换：`src/infra/network_manager.c`、`src/infra/vela_tls.c`
- MCP 静态容量：`src/tools/mcp_client.h`、`src/tools/mcp_client.c`
- TLS pool 容量：`src/infra/vela_tls.c`
- 音频预处理/增益：`src/voice/voice_channel.c`、`src/voice/audio_capture.c`
- CLI 条件命令：`src/channels/nsh_commands.c`

## 附录 B：参考资料

- Apache NuttX，Configuring：<https://nuttx.apache.org/docs/latest/quickstart/configuring.html>
- Apache NuttX，Compiling with CMake：<https://nuttx.apache.org/docs/latest/quickstart/compiling_cmake.html>
- Apache NuttX，Custom Application Directories：<https://nuttx.apache.org/docs/latest/guides/custom_app_directories.html>
- Apache NuttX，Make Build System：<https://nuttx.apache.org/docs/latest/implementation/make_build_system.html>

## 附录 C：分析限制

- 本报告基于指定 commit 的静态源码，不代表后续 `dev` 状态。
- 没有完整 openvela 源码树和板级 defconfig，因此未实际打开 menuconfig 或完成配置矩阵编译。
- Kconfig 所列 RAM 数字为项目自身估算；量产决策应以目标板 map 文件、heap 水位和并发压力测试为准。
