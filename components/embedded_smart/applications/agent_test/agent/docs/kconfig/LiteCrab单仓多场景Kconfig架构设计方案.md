# LiteCrab 单仓多场景 Kconfig 架构设计方案

> 目标：以 `LiteCrab_0829` 为唯一核心代码仓，同时支持“智能诊断”“机器人”以及后续新场景；通过 Kconfig/Profile 自动选择编译能力，场景只维护自己的适配器、工具和配置，不再复制并删减核心源码。  
> 分析日期：2026-09-28  
> 对照范围：`D:\项目\能源agent\code\LiteCrab_0829` 与机器人目录 `/code/3516/device/soc/hisilicon/common/platform/agent`（Windows 映射路径为 `Z:\code\3516\device\soc\hisilicon\common\platform\agent`）。

## 1. 执行摘要

当前有两份 LiteCrab：

- 主仓 `LiteCrab_0829` 面向智能诊断，包含 TCP Gateway、Alarm、PLC Diagnosis、MCP Client、Workspace 工具等；
- 机器人目录复制了部分核心代码，删除诊断模块，并直接修改 Kernel、LLM、Config、Builtin Tools，增加机器人 API、机器人工具和 Hi3516 构建文件。

这种方式短期能编译，长期会出现四类问题：

1. **核心修复无法自动同步**：Session、Memory、LLM、Tool Runtime 的缺陷要在两个目录重复修复；
2. **场景逻辑污染核心**：机器人 Prompt、Tool callback、SNI 适配直接改进 `kernel.c/llm.c`；
3. **构建系统各自维护**：主仓使用 CMake，机器人使用 OpenHarmony GN，源文件清单容易漂移；
4. **组合无法验证**：删除文件后只能验证两份固定代码，不能系统验证“关闭 Alarm、打开 MCP、机器人只读”等组合。

建议采用以下目标架构：

```text
                    单一 LiteCrab 仓库
┌──────────────────────────────────────────────────────────────┐
│ Core：Kernel / Runtime / Hub / Session / Memory / JSON      │
│       只提供稳定机制，不知道“诊断”或“机器人”                 │
├──────────────────────────────────────────────────────────────┤
│ Features：Skill / Workspace Tools / Alarm / MCP / Gateway   │
│           Robot Tools / TTS / Memory Tool / Observability    │
├──────────────────────────────────────────────────────────────┤
│ Scenarios：diagnostic / robot / custom                       │
│            只组合 Feature、Prompt、入口与默认资源参数         │
├──────────────────────────────────────────────────────────────┤
│ Platforms：posix-openssl / hi3516-ohos / 后续 openvela       │
│            线程、时钟、文件、LLM transport、构建适配          │
├──────────────────────────────────────────────────────────────┤
│ Kconfig + Profiles                                           │
│   → autoconf.h + config.cmake + config.gni + capability.json │
└──────────────────────────────────────────────────────────────┘
```

核心原则：

- **Profile 决定产品默认组合，Feature 决定实际编译内容，Runtime config 决定部署参数。**
- **核心模块不通过场景名写分支**，只依赖接口和 feature registry。
- **CMake 与 GN 不各自维护源文件表**，由一个组件 manifest 生成两套清单。
- **机器人目录只保留薄 BUILD.gn/产品 defconfig**，不再保存 LiteCrab 核心 `.c/.h` 副本。

## 2. 当前两份代码的实际差异

### 2.1 主仓独有能力

主仓存在而机器人副本删除的主要代码：

- `src/alarm/*`：告警轮询/订阅、持久 spool、通知和 ASP Client；
- `src/gateway/tcp_line.c`：TCP 行协议 Gateway；
- `src/mcp_client/*`：MCP Client、HTTP transport 和协议；
- `src/main.c`：诊断服务进程入口；
- `include/litecrab/alarm*.h`、`gateway.h`、`mcp_client.h`；
- 智能诊断所需的 Workspace/文件/程序执行类内置工具。

### 2.2 机器人副本独有能力

机器人目录增加：

- `agent_core.c`：`agent_init/input/deinit` 嵌入式 API；
- `include/agent/agent.h`：供 ASR、TTS、舵机模块调用的公共接口；
- `platform_3516.h`：平台兼容定义；
- `BUILD.gn`、`bundle.json`：OpenHarmony/Hi3516 构建入口；
- `agent_test.c`：机器人回调联调程序；
- `robot.move/robot.speak/robot.stop` 工具；
- Tool call 外部执行回调和 TTS 回调。

### 2.3 被直接修改的核心文件

对比显示以下共同文件已发生实质分叉：

- `src/config/config.c`：机器人删除 Gateway/MCP 配置和校验；
- `src/kernel/kernel.c`：替换系统 Prompt、公开内部 `run_round`、增加工具回调、改变栈分配；
- `src/kernel/llm.c`：大量替换为 Hi3516 的 LLM Client/网络适配；
- `src/runtime/builtin_tools.c`：机器人仅保留 `skill_read`、`skill_complete`、`memory_remember`；
- `src/kernel/skill.c`、`memory.c`、`observability.c`、`session.c` 等也存在差异；
- `include/litecrab/kernel.h`：增加 `vhost`、同步执行和工具回调接口。

这说明问题不只是“哪些文件编译”。必须先把核心文件中的可变点抽成接口，否则 Kconfig 只会把双份源码变成单文件内的大量条件分支。

### 2.4 需要立即处理的安全问题

机器人测试源码中存在硬编码 LLM 凭据。该值不应进入任何 Kconfig、defconfig、源码、BUILD.gn 或生成文件；应立即撤销/轮换，并改用环境变量、设备安全存储或构建系统的 secret 注入。本文不记录该凭据内容。

## 3. 设计目标与非目标

### 3.1 设计目标

1. 主仓只保留一份 Core 源码；
2. `diagnostic` 和 `robot` 从同一 commit 构建；
3. 选择 Profile 后自动得到正确 Feature、源码、依赖、宏和资源参数；
4. CMake 与 GN 对同一配置产生一致能力；
5. 场景模块可以注册 Prompt、工具、入口和回调，而不修改 Kernel；
6. 未启用功能不进入编译/链接，也不出现在运行时能力目录；
7. 配置组合可在 CI 中自动验证；
8. 后续增加新场景只增加 Profile/Feature/Adapter，不复制 Core。

### 3.2 非目标

- Kconfig 不保存 API key、密码、证书私钥；
- Kconfig 不替代运行时 JSON、环境变量或设备配置中心；
- 首期不要求任意 Feature 都能自由组合，生产 Profile 可以只支持验证过的组合；
- 不用 Kconfig 解决业务规则热更新；重新选择编译能力需要重新构建。

## 4. 目标分层架构

### 4.1 Core：永远保持场景无关

建议 Core 仅包含：

| Core 模块 | 职责 | 场景无关边界 |
|---|---|---|
| JSON | 严格 JSON token、读取和有界构建 | 不知道业务 schema |
| Hub/Message Bus | 请求、优先级、deadline、取消、完成投递 | 不知道请求来自 TCP、Alarm 或 ASR |
| Kernel/ReAct | 上下文装配、LLM/Tool 循环、完成状态 | 不内置诊断/机器人 Prompt |
| Runtime Registry | 工具注册、参数校验、权限、执行和过滤 | 不硬编码具体工具集合 |
| Session | Session 生命周期和持久化接口 | 存储路径由平台/配置提供 |
| Working Memory | 当前任务状态 | 不认识 PLC 或机器人动作 |
| Memory Core | 记忆存取机制 | `memory_remember` 是否注册由 Feature 决定 |
| LLM Protocol | 请求 JSON、响应解析、tool call adapter | 网络传输由 Platform Adapter 提供 |
| Lifecycle | Feature 按依赖启动、失败逆序回滚、停止 | 不直接调用 Alarm/Gateway |

Core 不允许出现：

```c
#if CONFIG_LITECRAB_SCENE_ROBOT
/* 机器人特例 */
#endif
```

Core 只允许依赖通用接口，如 `LiteCrabPromptProvider`、`LlmTransportOps`、`LiteCrabFeatureDescriptor`。

### 4.2 Feature：独立、可裁剪的能力单元

Feature 应包含自己的：

- 源文件清单；
- public/internal headers；
- Kconfig 依赖；
- runtime config schema；
- init/start/quiesce/stop/destroy；
- 工具注册；
- 测试；
- 外部库依赖；
- Flash/BSS/heap/线程成本说明。

建议首批 Feature：

```text
feature_gateway_tcp
feature_alarm
feature_mcp_client
feature_skills
feature_memory
feature_workspace_tools
feature_process_tool
feature_observability
feature_robot_api
feature_robot_tools
feature_tts_callback
```

### 4.3 Scenario：只负责组合

Scenario 不再拥有 Core 副本，只包含：

- 默认 Prompt；
- 默认 Feature 组合；
- 场景专属工具；
- 场景入口/API；
- 默认资源预算；
- defconfig；
- 场景 E2E 测试。

智能诊断场景：

```text
diagnostic
├── standalone server entry
├── TCP Gateway
├── Alarm
├── Skills + PLC_Diagnosis
├── Workspace/exec tools
├── Memory
├── MCP Client（按产品配置）
└── diagnostic prompt
```

机器人场景：

```text
robot
├── embedded library/API
├── ASR text input
├── TTS response callback
├── external actuator tool callback
├── robot.move/speak/stop tools
├── Skills（可选）
├── Memory（可选）
├── no TCP Gateway / no Alarm / no MCP by default
└── robot prompt
```

### 4.4 Platform Adapter：吸收平台差异

建议定义以下平台接口：

```text
platform/thread       pthread 创建、栈大小、join
platform/time         monotonic/realtime
platform/fs           mkdir、atomic replace、路径能力
platform/log          stdout/syslog/设备日志
platform/llm_transport OpenSSL socket 或 Hi3516 libllmclient
platform/secret       getenv 或设备安全存储
platform/process      fork/exec/resource limit，可能不支持
```

首期平台：

- `posix_openssl`：当前 WSL/SD5091 CMake 版本；
- `hi3516_ohos`：机器人 GN 版本，使用平台 `libllmclient.a`；
- 后续如需要，可增加 `openvela_nuttx`，不修改 Core。

## 5. 推荐目录结构

```text
LiteCrab/
├── Kconfig
├── defconfigs/
│   ├── diagnostic_defconfig
│   ├── robot_hi3516_defconfig
│   ├── minimal_defconfig
│   └── custom_example_defconfig
├── include/litecrab/
│   ├── core/*.h
│   ├── feature.h
│   ├── platform.h
│   ├── scenario.h
│   └── generated_config.h      # wrapper，只 include 构建目录 autoconf.h
├── src/core/
│   ├── kernel/
│   ├── runtime/
│   ├── hub/
│   ├── session/
│   ├── memory/
│   ├── llm_protocol/
│   └── json/
├── src/features/
│   ├── gateway_tcp/
│   ├── alarm/
│   ├── mcp_client/
│   ├── skills/
│   ├── workspace_tools/
│   ├── memory_tool/
│   └── observability/
├── src/scenarios/
│   ├── diagnostic/
│   │   ├── prompt.c
│   │   ├── composition.c
│   │   └── main.c
│   └── robot/
│       ├── prompt.c
│       ├── robot_tools.c
│       ├── agent_api.c
│       └── include/agent/agent.h
├── src/platform/
│   ├── posix_openssl/
│   └── hi3516_ohos/
├── build/
│   ├── components.py           # 唯一源码/依赖 manifest
│   ├── cmake/
│   └── gn/
├── tools/kconfig/
│   ├── configure.py
│   ├── menuconfig.py
│   └── kconfiglib.py           # 固定版本或受控依赖
└── tests/
    ├── core/
    ├── features/
    ├── scenarios/
    └── config_matrix/
```

不要把生成的 `autoconf.h` 提交到公共源码目录，避免切换 Profile 后污染其他构建。每个 build directory 拥有自己的生成配置。

## 6. Kconfig 菜单方案

### 6.1 顶层结构

建议配置前缀统一使用 `LITECRAB_`：

```kconfig
mainmenu "LiteCrab Configuration"

menu "Product profile"
choice
    prompt "LiteCrab scenario"
    default LITECRAB_SCENE_DIAGNOSTIC

config LITECRAB_SCENE_DIAGNOSTIC
    bool "Smart diagnosis"

config LITECRAB_SCENE_ROBOT
    bool "Robot"

config LITECRAB_SCENE_CUSTOM
    bool "Custom composition"
endchoice
endmenu

menu "Platform"
choice
    prompt "Target platform"

config LITECRAB_PLATFORM_POSIX_OPENSSL
    bool "POSIX + OpenSSL"

config LITECRAB_PLATFORM_HI3516_OHOS
    bool "Hi3516 OpenHarmony"
endchoice
endmenu
```

### 6.2 入口形态

```kconfig
choice
    prompt "Application integration mode"

config LITECRAB_APP_STANDALONE
    bool "Standalone service executable"
    default y if LITECRAB_SCENE_DIAGNOSTIC

config LITECRAB_APP_EMBEDDED
    bool "Embedded library API"
    default y if LITECRAB_SCENE_ROBOT
endchoice
```

这将当前 `src/main.c` 和机器人 `agent_core.c` 明确成两种入口，而不是互相替换 Core。

### 6.3 功能开关

```kconfig
menu "Features"

config LITECRAB_FEATURE_GATEWAY_TCP
    bool "TCP Gateway"
    default y if LITECRAB_SCENE_DIAGNOSTIC
    depends on LITECRAB_APP_STANDALONE

config LITECRAB_FEATURE_ALARM
    bool "Alarm ingestion and durable spool"
    default y if LITECRAB_SCENE_DIAGNOSTIC
    depends on LITECRAB_FEATURE_GATEWAY_TCP

config LITECRAB_FEATURE_MCP_CLIENT
    bool "MCP Client"
    default y if LITECRAB_SCENE_DIAGNOSTIC
    depends on LITECRAB_PLATFORM_HAS_HTTP_CLIENT

config LITECRAB_FEATURE_SKILLS
    bool "Skills"
    default y

config LITECRAB_FEATURE_MEMORY
    bool "Long-term memory"
    default y

config LITECRAB_FEATURE_WORKSPACE_TOOLS
    bool "Workspace read/write/search tools"
    default y if LITECRAB_SCENE_DIAGNOSTIC
    depends on LITECRAB_PLATFORM_HAS_FILESYSTEM

config LITECRAB_FEATURE_PROCESS_TOOL
    bool "Restricted program execution tool"
    default y if LITECRAB_SCENE_DIAGNOSTIC
    depends on LITECRAB_PLATFORM_HAS_PROCESS

config LITECRAB_FEATURE_ROBOT_API
    bool "Robot embedded API"
    default y if LITECRAB_SCENE_ROBOT
    depends on LITECRAB_APP_EMBEDDED

config LITECRAB_FEATURE_ROBOT_TOOLS
    bool "Robot motion/speech tools"
    default y if LITECRAB_SCENE_ROBOT
    depends on LITECRAB_FEATURE_ROBOT_API

config LITECRAB_FEATURE_TTS_CALLBACK
    bool "TTS output callback"
    default y if LITECRAB_SCENE_ROBOT

endmenu
```

### 6.4 LLM transport

当前主仓 `llm.c` 使用 OpenSSL/socket，机器人副本使用平台 `libllmclient` 并增加 connect host/vhost 区分。应改为接口选择：

```kconfig
choice
    prompt "LLM transport"

config LITECRAB_LLM_TRANSPORT_OPENSSL
    bool "Built-in POSIX/OpenSSL transport"
    default y if LITECRAB_PLATFORM_POSIX_OPENSSL

config LITECRAB_LLM_TRANSPORT_EXTERNAL
    bool "Platform external LLM client"
    default y if LITECRAB_PLATFORM_HI3516_OHOS
endchoice
```

LLM JSON 构造、响应解析和 Tool adapter 保持 Core 唯一；只有 HTTP/TLS 收发放到 transport adapter。

### 6.5 工具组

当前 `builtin_tools.c` 过大，机器人通过复制后删除 1100 多行实现裁剪。建议先拆文件，再用工具组开关：

```kconfig
config LITECRAB_TOOL_FILE_READ
config LITECRAB_TOOL_FILE_WRITE
config LITECRAB_TOOL_FILE_SEARCH
config LITECRAB_TOOL_EXEC_PROGRAM
config LITECRAB_TOOL_SHELL
config LITECRAB_TOOL_SKILL
config LITECRAB_TOOL_MEMORY
config LITECRAB_TOOL_ROBOT
```

不建议为每个普通函数建立 Kconfig。以“具有独立权限、依赖或资源成本的工具组”为粒度，既能裁剪，也不会让菜单失控。

### 6.6 资源参数

建议首批开放：

```kconfig
config LITECRAB_MAX_TOOL_ITERATIONS
    int "Maximum tool iterations"
    range 1 40
    default 16 if LITECRAB_SCENE_DIAGNOSTIC
    default 5 if LITECRAB_SCENE_ROBOT

config LITECRAB_TOOL_REGISTRY_CAPACITY
    int "Maximum registered tools"
    range 4 128
    default 32

config LITECRAB_LLM_MAX_TOOL_CALLS
    int "Maximum tool calls per LLM response"
    range 1 16
    default 8

config LITECRAB_LLM_RESPONSE_BYTES
    int "Maximum LLM response bytes"
    range 8192 262144
    default 65536

config LITECRAB_AGENT_THREAD_STACK
    int "Agent thread stack bytes"
    range 65536 1048576
    default 524288 if LITECRAB_PLATFORM_HI3516_OHOS
    default 262144

config LITECRAB_SESSION_CAPACITY
    int "Maximum active sessions"
    range 1 256
    default 1 if LITECRAB_SCENE_ROBOT
    default 128 if LITECRAB_SCENE_DIAGNOSTIC
```

这些值应替换当前散落在头文件、机器人 `agent_core.c` 和 `pthread_attr_setstacksize()` 中的硬编码。

### 6.7 平台能力 hidden symbols

平台选择应派生不可直接编辑的 capability：

```kconfig
config LITECRAB_PLATFORM_HAS_FILESYSTEM
    bool
    default y

config LITECRAB_PLATFORM_HAS_PROCESS
    bool
    default y if LITECRAB_PLATFORM_POSIX_OPENSSL

config LITECRAB_PLATFORM_HAS_HTTP_CLIENT
    bool
    default y
```

Feature 依赖 capability，而不是直接依赖具体平台名。以后增加 openvela 时无需修改所有 Feature。

## 7. Profile 方案

### 7.1 为什么 Kconfig 之外还要 Profile

Kconfig 描述“允许的配置空间”，Profile 描述“经过验证的产品组合”。只提供 menuconfig 会导致每个人构建出不同固件；只提供固定脚本又会回到多份维护。

建议仓库提交以下 defconfig：

### 7.2 `diagnostic_defconfig`

```text
CONFIG_LITECRAB_SCENE_DIAGNOSTIC=y
CONFIG_LITECRAB_PLATFORM_POSIX_OPENSSL=y
CONFIG_LITECRAB_APP_STANDALONE=y
CONFIG_LITECRAB_FEATURE_GATEWAY_TCP=y
CONFIG_LITECRAB_FEATURE_ALARM=y
CONFIG_LITECRAB_FEATURE_MCP_CLIENT=y
CONFIG_LITECRAB_FEATURE_SKILLS=y
CONFIG_LITECRAB_FEATURE_MEMORY=y
CONFIG_LITECRAB_FEATURE_WORKSPACE_TOOLS=y
CONFIG_LITECRAB_FEATURE_PROCESS_TOOL=y
CONFIG_LITECRAB_FEATURE_ROBOT_API=n
CONFIG_LITECRAB_TOOL_SHELL=n
```

### 7.3 `robot_hi3516_defconfig`

```text
CONFIG_LITECRAB_SCENE_ROBOT=y
CONFIG_LITECRAB_PLATFORM_HI3516_OHOS=y
CONFIG_LITECRAB_APP_EMBEDDED=y
CONFIG_LITECRAB_FEATURE_GATEWAY_TCP=n
CONFIG_LITECRAB_FEATURE_ALARM=n
CONFIG_LITECRAB_FEATURE_MCP_CLIENT=n
CONFIG_LITECRAB_FEATURE_SKILLS=y
CONFIG_LITECRAB_FEATURE_MEMORY=y
CONFIG_LITECRAB_FEATURE_WORKSPACE_TOOLS=n
CONFIG_LITECRAB_FEATURE_PROCESS_TOOL=n
CONFIG_LITECRAB_FEATURE_ROBOT_API=y
CONFIG_LITECRAB_FEATURE_ROBOT_TOOLS=y
CONFIG_LITECRAB_FEATURE_TTS_CALLBACK=y
CONFIG_LITECRAB_LLM_TRANSPORT_EXTERNAL=y
CONFIG_LITECRAB_MAX_TOOL_ITERATIONS=5
CONFIG_LITECRAB_SESSION_CAPACITY=1
```

### 7.4 `minimal_defconfig`

仅 Core + LLM + Runtime，不带 Gateway、Alarm、MCP、Skills、Memory 和平台业务工具，用于单元测试与移植基线。

## 8. 配置生成方案

LiteCrab 同时面对 CMake 和 OpenHarmony GN，不能直接把 openvela/NuttX 的 `<nuttx/config.h>` 机制照搬过来。建议用固定版本的 Kconfig parser 在构建主机生成四种输出：

```text
build/<profile>/.config
build/<profile>/generated/include/litecrab/autoconf.h
build/<profile>/generated/litecrab_config.cmake
build/<profile>/generated/litecrab_config.gni
build/<profile>/generated/litecrab_capabilities.json
```

含义：

- `.config`：权威选择结果，可 diff；
- `autoconf.h`：C 源码的 `CONFIG_LITECRAB_*`；
- `.cmake`：CMake bool/int/string；
- `.gni`：GN bool/int/string；
- `capabilities.json`：打包、诊断和 CI 使用的能力清单，不参与 secret 配置。

统一入口：

```sh
python3 tools/kconfig/configure.py \
  --profile diagnostic \
  --out build/diagnostic

python3 tools/kconfig/configure.py \
  --profile robot_hi3516 \
  --out out/robot/litecrab
```

交互修改：

```sh
python3 tools/kconfig/menuconfig.py \
  --config build/diagnostic/.config
```

`configure.py` 必须做：

1. 加载 Kconfig；
2. 加载 profile defconfig；
3. 可选加载本地 override fragment；
4. 执行依赖求值；
5. 将 warning 视为 CI error；
6. 生成所有后端文件；
7. 输出配置 hash；
8. 验证源文件和外部依赖存在。

## 9. 单一组件 Manifest

仅有 Kconfig 仍不能阻止 CMake/GN 源文件表漂移。建议建立一个 Python/JSON manifest：

```python
COMPONENTS = {
    "core": {
        "sources": ["src/core/..."],
        "requires": [],
    },
    "feature_alarm": {
        "kconfig": "CONFIG_LITECRAB_FEATURE_ALARM",
        "sources": ["src/features/alarm/..."],
        "libs": ["curl"],
    },
    "scenario_robot": {
        "kconfig": "CONFIG_LITECRAB_SCENE_ROBOT",
        "sources": ["src/scenarios/robot/agent_api.c", "..."],
    },
}
```

生成器根据 `.config` 输出：

- `LITECRAB_SOURCES`/`LITECRAB_LIBS` 到 CMake；
- `litecrab_sources`/`litecrab_deps` 到 GNI；
- capability manifest 到 JSON。

这样源文件归属只维护一次。CMake 和 GN 只消费结果，不再各自复制几十行清单。

## 10. CMake 接入设计

```cmake
set(LITECRAB_PROFILE "diagnostic" CACHE STRING "LiteCrab profile")
set(LITECRAB_CONFIG_DIR
    "${CMAKE_BINARY_DIR}/litecrab-config" CACHE PATH "Generated config dir")

execute_process(
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/kconfig/configure.py
          --profile ${LITECRAB_PROFILE}
          --out ${LITECRAB_CONFIG_DIR}
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "LiteCrab Kconfig generation failed")
endif()

include(${LITECRAB_CONFIG_DIR}/generated/litecrab_config.cmake)
include(${LITECRAB_CONFIG_DIR}/generated/litecrab_sources.cmake)

add_library(litecrab_core STATIC ${LITECRAB_CORE_SOURCES})
target_include_directories(litecrab_core PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${LITECRAB_CONFIG_DIR}/generated/include)

if(CONFIG_LITECRAB_APP_STANDALONE)
  add_executable(litecrab_server ${LITECRAB_ENTRY_SOURCES})
endif()
```

兼容现有命令：

```sh
cmake -S . -B build-diagnostic \
  -DLITECRAB_PROFILE=diagnostic \
  -DCMAKE_TOOLCHAIN_FILE=aarch32-toolchain.cmake
```

现有 `LITECRAB_WITH_MCP_CLIENT` 等 CMake option 在迁移期可映射到 Kconfig override，但最终应删除双重真相源。

## 11. OpenHarmony GN 接入设计

推荐把唯一 LiteCrab 源码放入 OpenHarmony 树的共享位置，例如：

```text
third_party/litecrab/                # repo/submodule，唯一源码
device/soc/hisilicon/.../agent/
├── BUILD.gn                         # 薄包装
└── robot_hi3516_defconfig           # 产品选择
```

设备目录不再复制 `src/` 和 `include/litecrab/`。

薄 `BUILD.gn`：

```gn
import("//build/lite/config/component/lite_component.gni")
import("$litecrab_generated_dir/generated/litecrab_config.gni")
import("$litecrab_generated_dir/generated/litecrab_sources.gni")

static_library("libagent") {
  sources = litecrab_sources
  include_dirs = [
    "//third_party/litecrab/include",
    "$litecrab_generated_dir/generated/include",
  ]
  deps = litecrab_gn_deps
  cflags = litecrab_cflags
}
```

GN 生成前执行：

```sh
python3 third_party/litecrab/tools/kconfig/configure.py \
  --profile robot_hi3516 \
  --out out/<product>/litecrab-config
```

然后由产品构建参数传入：

```text
litecrab_generated_dir = "//out/<product>/litecrab-config"
```

如果 OpenHarmony 构建编排不方便增加预步骤，可由 GN `exec_script()` 调用同一生成器，但 CI 和本地增量构建更推荐显式预生成，错误更清楚。

不建议使用 Windows/WSL 文件系统软链接维护同一份源码；repo manifest、git submodule 或受控源码同步更可复现。

## 12. Feature 生命周期设计

当前 `main.c` 手工依次调用 Agent、MCP、Alarm、Gateway；机器人 `agent_core.c` 又维护另一套顺序。建议统一为 Feature Descriptor：

```c
typedef struct LiteCrabFeatureDescriptor {
    const char* name;
    int  (*load_config)(LiteCrabConfigBuilder*);
    int  (*register_tools)(CrabRuntime*);
    int  (*init)(LiteCrabContext*);
    int  (*start)(LiteCrabContext*);
    int  (*quiesce)(LiteCrabContext*);
    void (*stop)(LiteCrabContext*);
    void (*destroy)(LiteCrabContext*);
} LiteCrabFeatureDescriptor;
```

组合文件只声明启用组件：

```c
static const LiteCrabFeatureDescriptor* features[] = {
    &litecrab_feature_core,
#if CONFIG_LITECRAB_FEATURE_SKILLS
    &litecrab_feature_skills,
#endif
#if CONFIG_LITECRAB_FEATURE_MCP_CLIENT
    &litecrab_feature_mcp_client,
#endif
#if CONFIG_LITECRAB_FEATURE_ALARM
    &litecrab_feature_alarm,
#endif
};
```

Lifecycle manager 统一实现：

- 正序 `load_config → init → register_tools → start`；
- 任一步失败，逆序回滚已成功组件；
- 停机 `quiesce → stop → destroy`；
- 输出实际启用 Feature 和配置 hash。

这样新增场景不再复制 `main.c`。

## 13. Prompt 架构

机器人副本直接把 `ContextBuildSystemPromptEx()` 中的 LiteCrab Prompt 改成 Robot Prompt。这是典型场景注入点，应改成 provider：

```c
typedef struct {
    const char* id;
    int (*append_identity)(CrabTextBuilder*);
    int (*append_rules)(CrabTextBuilder*);
    int (*append_domain_context)(CrabTextBuilder*, const LiteCrabRequest*);
} LiteCrabPromptProvider;
```

Core 负责固定安全和 Tool 协议规则；Scenario 只能提供：

- 身份和语气；
- 业务目标；
- 场景上下文；
- Tool 使用建议。

安全规则、工具 schema 权威性、完成语义和输入不可信原则必须由 Core 追加，避免场景 Prompt 覆盖基础安全约束。

## 14. LLM 架构重构

当前 `llm.c` 混合了：

- URL 解析；
- DNS/socket；
- OpenSSL；
- HTTP framing；
- JSON request；
- LLM response/tool call 解析；
- deadline/cancel。

应拆为：

```text
llm_protocol.c       OpenAI-compatible request/response/tool call
llm_client.c         retry/deadline/cancel/metrics
transport_openssl.c  POSIX socket + OpenSSL
transport_hi3516.c   libllmclient.a adapter
```

接口：

```c
typedef struct {
    int (*request)(const LiteCrabHttpRequest*, LiteCrabHttpResponse*);
    void (*cancel)(void);
    void (*destroy)(void);
} LlmTransportOps;
```

`connectHost` 和 `tlsServerName/httpHost` 应作为通用请求字段，而不是机器人私有 `vhost` 补丁。这样 IP 直连、SNI 域名和 Host header 的关系在两个平台上保持一致。

## 15. Tool 执行架构

机器人把 Tool call callback 直接加进 Kernel，并在设置回调时绕过 `CrabRuntimeCallTool()`。这会绕过 Runtime 的 schema、权限、风险、并发和过滤边界，不建议作为最终实现。

推荐把外部舵机执行器实现为正常 Tool backend：

```c
typedef int (*CrabExternalToolExecuteFn)(
    void* context,
    const CrabToolSpec* spec,
    const CrabToolCall* call,
    CrabRawResult* raw);
```

执行链保持：

```text
LLM tool_call
  → Registry 查找 ToolSpec
  → schema/required/range/enum 校验
  → permission/risk/concurrency 检查
  → Robot backend callback
  → RawResult
  → Tool filter
  → 有界 ToolResponse
  → 回到 LLM
```

机器人模块只注册 `robot.move/speak/stop` 的 ToolSpec 和 backend，Kernel 无需知道舵机存在。

`agent_register_tool(parameters_json)` 当前没有解析 schema，只注册空参数 Tool。应统一使用结构化 `CrabToolSpec`，或者完整、严格地解析 JSON Schema；不能让 LLM 看到的 schema 与运行时验证不一致。

## 16. 统一嵌入式 API

机器人需要的能力应成为主仓正式 API，而不是公开内部 `run_round()`：

```c
typedef struct LiteCrabAgent LiteCrabAgent;

typedef struct {
    LiteCrabCoreConfig core;
    LlmConfig llm;
    const LiteCrabScenarioDescriptor* scenario;
    const LiteCrabPlatformOps* platform;
} LiteCrabAgentOptions;

int LiteCrabAgentCreate(const LiteCrabAgentOptions*, LiteCrabAgent**);
int LiteCrabAgentStart(LiteCrabAgent*);
int LiteCrabAgentSubmit(LiteCrabAgent*, const LiteCrabRequest*);
int LiteCrabAgentExecuteSync(LiteCrabAgent*,
                             const LiteCrabRequest*,
                             LiteCrabResponse*);
void LiteCrabAgentStop(LiteCrabAgent*);
void LiteCrabAgentDestroy(LiteCrabAgent*);
```

机器人 `agent_init/input/deinit` 可以作为这一 API 的兼容薄封装。同步接口也必须经过统一 Session、deadline、trace、Tool Runtime 和完成状态，不能直接调用 Kernel 私有函数绕过消息总线约束。

## 17. 配置模块拆分

当前 `LiteCrabAppConfig` 直接包含 `RequestServerConfig` 和 `McpClientConfig`，导致机器人删除 Gateway/MCP 后还必须修改 `config.h/config.c`。

建议拆分：

```text
LiteCrabCoreConfig
├── workspace
├── LLM
├── Session/Memory
├── Runtime limits
└── Observability basic

GatewayConfig       # gateway feature 私有
AlarmConfig         # alarm feature 私有
McpClientConfig     # MCP feature 私有
RobotConfig         # robot scenario 私有
```

每个 Feature 解析自己的 JSON section。Core config loader 只负责：

- 读取文档；
- Provider/环境变量合并；
- 调用启用 Feature 的 parser；
- 汇总错误；
- 生成只读运行快照。

这样关闭一个 Feature 时无需从大结构体和大解析函数中删除字段。

## 18. 编译期与运行时配置边界

| 内容 | Kconfig/Profile | Runtime JSON/环境变量 |
|---|---|---|
| 是否编译 Alarm/MCP/Gateway/Robot | 是 | 否 |
| Tool registry 最大容量 | 是 | 否 |
| 线程栈、静态 buffer 上限 | 是 | 否 |
| LLM transport 实现 | 是 | 否 |
| LLM URL/model/timeout | 默认上限可在 Kconfig；实际值在运行时 | 是 |
| API key/token/password | **禁止** | 环境变量/安全存储 |
| Workspace/日志路径 | 可提供默认值 | 是 |
| 是否启用已编译工具 | 可设量产硬上限 | 运行时可进一步关闭 |
| Prompt 业务内容 | Scenario 源文件/受控资源 | 可选版本化加载，但须安全校验 |

规则：运行时只能收紧或配置已编译能力，不能越过 Kconfig 打开未编译能力或突破编译期资源上限。

## 19. 单仓接入方式

### 19.1 SD5091/智能诊断

继续在 LiteCrab 主仓使用 CMake：

```sh
cmake -S . -B build-5091 \
  -DLITECRAB_PROFILE=diagnostic \
  -DCMAKE_TOOLCHAIN_FILE=aarch32-toolchain.cmake \
  -DSD5091_SYSROOT=/opt/sd5091-sysroot
cmake --build build-5091 -j
```

### 19.2 Hi3516/机器人

OpenHarmony 工程通过 repo manifest 或 git submodule 把同一 LiteCrab commit 放在 `third_party/litecrab`。设备目录只提供：

- `robot_hi3516_defconfig` 或产品对该 profile 的引用；
- 薄 `BUILD.gn`；
- 平台依赖路径，例如 network/libllmclient；
- 板级 tool callback/TTS callback 实现。

不再执行“复制 core → 删除目录 → 修改共同文件”。

### 19.3 版本锁定

机器人产品仓锁定 LiteCrab commit/tag，例如：

```text
LiteCrab release: v1.2.0
Config profile: robot_hi3516
Config hash: sha256:...
Platform adapter ABI: 1
```

固件版本信息应包含这三项，便于现场定位“代码相同但配置不同”的问题。

## 20. 迁移步骤

### 阶段 0：止血与基线

1. 立即撤销机器人测试源码中的硬编码凭据；
2. 对主仓和机器人副本打基线 tag；
3. 固化两边现有 smoke/E2E 测试和二进制行为；
4. 记录机器人副本相对主仓的每个语义差异，禁止继续手工同步。

验收：两边原构建仍可复现，凭据扫描通过。

### 阶段 1：先抽扩展点，不上 Kconfig

1. 从 `kernel.c` 抽 Prompt provider；
2. 从 `llm.c` 抽 transport ops；
3. 从 Kernel 移除机器人 tool callback 特例，改为 Runtime external backend；
4. 建立正式 sync/async Agent API；
5. 拆分 `builtin_tools.c`；
6. 拆分 Core config 和 feature config。

验收：主仓仍以默认 CMake 构建，行为不变；机器人能力可以作为新目录在主仓编译。

### 阶段 2：引入 Kconfig 和生成器

1. 增加 `Kconfig`、三个 defconfig 和固定版本 parser；
2. 生成 `autoconf.h/cmake/gni/capabilities.json`；
3. 把现有 CMake options 映射到 Kconfig；
4. 添加依赖校验和配置 hash。

验收：`diagnostic`、`robot_hi3516`、`minimal` 三个 Profile 都能生成确定配置。

### 阶段 3：统一源文件清单

1. 建立组件 manifest；
2. CMake 和 GN 改为消费生成清单；
3. CI 检查 manifest 中所有文件存在、Feature 依赖闭合；
4. 删除重复 CMake/GN 源码枚举。

验收：同一 `.config` 的 capability list 在 CMake/GN 完全一致。

### 阶段 4：机器人切换到单仓

1. 把机器人 `agent_core.c/agent.h/robot_tools` 合并到主仓 `scenario/robot`；
2. 把 Hi3516 LLM 适配合并到 `platform/hi3516_ohos`；
3. 设备树改为引用 `third_party/litecrab`；
4. 机器人执行 Profile 构建和板上回归；
5. 删除设备树内复制的 Core 源码。

验收：机器人构建产物只引用唯一 LiteCrab 源码；原有 ASR → Agent → Tool/TTS 链路通过。

### 阶段 5：删除兼容层

1. 删除旧 `LITECRAB_WITH_MCP_*` CMake options；
2. 删除旧复制/同步脚本；
3. 清理兼容宏和 deprecated API；
4. 发布第一个单仓多场景版本。

## 21. 测试与 CI 矩阵

### 21.1 必须编译的 Profile

| Profile | CMake native | SD5091 cross | GN/Hi3516 | 单元测试 | E2E |
|---|---:|---:|---:|---:|---:|
| minimal | 必须 | 可选 | 可选 | Core | 初始化/一次对话 |
| diagnostic | 必须 | 必须 | 不要求 | Core + Alarm/MCP/Gateway | TCP + Alarm + Skill |
| robot_hi3516 | 可用 stub 编译 | 不要求 | 必须 | Core + Robot tools | ASR text + tool callback + TTS |
| custom/full | 必须 | 可选 | 可选 | 依赖闭合 | 能力枚举 |

### 21.2 配置静态检查

- Kconfig warning 为零；
- 未定义 symbol 引用为零；
- 每个 manifest source 文件存在；
- Feature 依赖闭合；
- 不兼容组合在配置阶段失败，而不是链接时失败；
- 生成文件可重复：相同输入 hash 和输出一致；
- defconfig 不包含 secret；
- 所有已编译 Feature 出现在 capability manifest；
- 所有未编译 Feature 的符号不出现在最终链接产物中。

### 21.3 行为测试

Robot：

- `robot.move/speak/stop` schema 校验；
- callback 成功、失败、超时；
- TTS callback；
- 单 Session/多轮历史；
- LLM transport 的 connect host/SNI/Host；
- 无 Gateway/Alarm/MCP 符号和线程。

Diagnostic：

- 当前全部测试继续通过；
- Alarm spool/ACK/restart；
- TCP Gateway；
- MCP enabled/disabled；
- PLC Skill；
- Workspace path 和进程工具权限。

### 21.4 资源回归

每个 Profile 保存：

- ELF text/data/bss；
- 静态库大小；
- 线程数和 stack 总预算；
- heap 峰值；
- 文件描述符峰值；
- 启动时间；
- 一轮无 Tool/单 Tool/多 Tool 的峰值。

超过 Profile 基线阈值则 CI 失败或要求人工批准。

## 22. 风险与规避

| 风险 | 影响 | 规避 |
|---|---|---|
| 在 Core 中大量使用场景 `#ifdef` | 形成“单仓内双份逻辑” | 先抽接口，再加开关 |
| Kconfig、CMake option、GN arg 三套真相 | 配置漂移 | Kconfig 唯一，其他均生成 |
| Feature 关闭但结构体仍依赖其头文件 | Robot 仍无法裁剪 | feature-owned config/headers |
| Tool callback 绕过 Runtime | 丧失校验和权限边界 | external backend 接入正常执行链 |
| Profile 默认值被个人修改 | 产物不可复现 | 提交 defconfig、输出 hash、CI 校验 |
| 生成文件写入源码树 | 多 Profile 相互污染 | 只写 build/out 目录 |
| Hi3516 与 POSIX transport 行为不同 | 同模型表现不一致 | transport contract tests |
| Feature 数量过细 | Kconfig 难维护 | 按权限/依赖/资源成本分组 |
| 继续保留设备树 Core 副本 | 修复仍需双写 | 完成切换后删除副本 |

## 23. 实施优先级

### P0

1. 轮换机器人源码中暴露的凭据，并加 secret scan；
2. 抽取 LLM transport；
3. 抽取 Prompt provider；
4. 把机器人 Tool callback 纳入 Runtime 正常校验链；
5. 建立正式 embedded Agent API。

### P1

1. 拆分 Builtin Tools 和 Feature config；
2. 引入 Kconfig/Profile/生成器；
3. CMake 使用生成配置；
4. GN 使用同一配置和 manifest；
5. 建立三 Profile CI。

### P2

1. 资源参数 Kconfig 化；
2. capability manifest 和配置 hash 写入版本信息；
3. 新平台适配模板；
4. 删除旧 CMake options、复制源码和同步脚本。

## 24. 可交付的最小版本范围

首版不要一次配置所有模块。建议最小可交付范围：

1. Profile：`diagnostic`、`robot_hi3516`、`minimal`；
2. Platform：`posix_openssl`、`hi3516_ohos`；
3. Feature：Gateway、Alarm、MCP、Skills、Memory、Workspace Tools、Robot API、Robot Tools；
4. Tool group：Workspace、Process、Skill、Memory、Robot；
5. Resource：Tool iterations、Agent stack、registry capacity、response bytes；
6. 输出：autoconf.h、CMake、GNI、capability JSON；
7. CI：3 Profile 编译 + 现有测试 + Robot stub E2E。

完成这个范围后，即可删除机器人 Core 副本；其他配置项可以后续迭代。

## 25. 验收标准

方案落地完成应同时满足：

- `LiteCrab_0829` 是唯一 Core 源码；
- 智能诊断和机器人由同一 commit 构建；
- 切换场景只改变 profile/defconfig，不复制或删除源码；
- 机器人设备目录没有 `src/kernel`、`src/runtime` 等副本；
- Core 文件不出现 `SCENE_ROBOT/SCENE_DIAGNOSTIC` 条件分支；
- 机器人 Tool call 仍经过 Registry、schema、权限和过滤；
- CMake 与 GN capability manifest 一致；
- 未启用模块不进入最终符号表；
- 三个 Profile CI 全绿；
- 所有生成配置不含 secret；
- 固件能够报告 LiteCrab commit、profile、config hash 和 platform adapter ABI。

## 26. 最终建议

这次改造的重点不是“增加一个 Kconfig 文件”，而是把 LiteCrab 从单场景程序重构为一个可组合的 Agent 产品平台。

正确顺序是：

```text
识别两份代码差异
  → 抽取 Core 扩展点
  → 模块化 Feature/Scenario/Platform
  → 引入 Kconfig 和 Profile
  → 生成 CMake/GN 配置与统一源清单
  → CI 验证配置矩阵
  → 删除机器人 Core 副本
```

如果直接在当前大文件中加入大量 `#ifdef ROBOT`，虽然能暂时做到一仓编译，但维护成本会从“两个目录”转移成“一个目录里的两套分支”，核心模型仍然不是共享的。只有让场景差异通过 Prompt provider、LLM transport、Feature lifecycle 和 Tool backend 接口注入，Kconfig 才能真正实现“一份核心、多种产品”。

## 附录 A：当前代码到目标模块的迁移映射

| 当前代码 | 目标位置 |
|---|---|
| `src/kernel/kernel.c` | Core ReAct + Prompt provider 接口 |
| `src/kernel/llm.c` | `llm_protocol` + platform transport |
| `src/runtime/builtin_tools.c` | 多个 tool group feature |
| `src/config/config.c` | Core loader + feature-owned parser |
| `src/main.c` | `scenario/diagnostic/main.c` |
| 机器人 `agent_core.c` | `scenario/robot/agent_api.c` |
| 机器人 `robot_tools[]` | `scenario/robot/robot_tools.c` |
| 机器人 `platform_3516.h` | `platform/hi3516_ohos/` |
| 机器人 `BUILD.gn` | `build/gn` + 设备树薄 wrapper |
| `src/alarm/*` | `features/alarm/` |
| `src/gateway/*` | `features/gateway_tcp/` |
| `src/mcp_client/*` | `features/mcp_client/` |

## 附录 B：本方案与 openvela Kconfig 的关系

openvela 项目展示了 Kconfig 控制源文件、预处理分支和资源参数的基本方法。LiteCrab 还需要额外解决两个问题：

1. 同时支持独立 CMake 与 OpenHarmony GN；
2. 现有机器人版本已经修改 Core，必须先抽象差异点。

因此 LiteCrab 应复用 Kconfig 的配置模型，但使用自己的生成层输出 CMake/GNI/autoconf，而不是依赖 `<nuttx/config.h>`。如果未来移植到 openvela/NuttX，可以再增加一个 exporter，把同一 Feature symbol 映射到 NuttX package Kconfig。

## 附录 C：分析限制

- 本文基于当前可读取的主仓和机器人目录静态分析，没有修改两边源码。
- 尚未在 Hi3516/OpenHarmony 全量构建环境中验证 `hb/gn` 的具体接入命令；GN wrapper 和生成目录参数需要结合产品构建脚本落地。
- 机器人副本的部分差异可能是临时调试代码，迁移时应逐项通过行为测试决定保留、上收 Core 或删除。
