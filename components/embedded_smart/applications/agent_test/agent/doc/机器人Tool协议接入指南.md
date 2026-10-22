# 机器人 Tool 协议接入指南（舵机下游适配）

本文档定义 agent 通过 LLM 下发给舵机模块的工具调用协议。舵机下游按此协议实现回调函数，即可接收并执行机器人动作指令。

---

## 1. 协议总览

### 1.1 工具列表

| 工具名 | 用途 | 是否需要舵机执行 |
|---|---|---|
| `robot.move` | 控制机器人向指定方向移动指定步数 | 是 |
| `robot.speak` | 控制机器人语音播报一段文本 | 是（TTS/扬声器） |
| `robot.stop` | 停止机器人当前动作 | 是 |

> 这三个工具由 agent 定义（`agent_core.c`），通过 LLM 的 OpenAI function-calling 协议下发。agent 自身不执行实际硬件动作，而是通过 **tool_call 回调** 把 `(工具名, 参数JSON)` 输出给下游舵机模块执行。

### 1.2 回调接口签名

下游需实现以下 C 函数（定义见 `include/litecrab/kernel.h:217` 与 `include/agent/agent.h:25`）：

```c
typedef int (*agent_tool_call_callback_fn)(const char* name,
                                           const char* args_json,
                                           char* result,
                                           size_t result_max);
```

**参数说明**

| 参数 | 方向 | 说明 |
|---|---|---|
| `name` | 入 | 工具名，如 `"robot.move"` / `"robot.speak"` / `"robot.stop"` |
| `args_json` | 入 | LLM 下发的参数 JSON 字符串（见各工具章节），可能为 `"{}"` |
| `result` | 出 | 执行结果缓冲区，由下游写入结果字符串（建议 JSON） |
| `result_max` | 入 | `result` 缓冲区最大容量，写入内容不得超过此长度 |
| **返回值** | 出 | `0` = 成功；非 `0` = 失败（agent 会回写 `"tool execution failed: N"`） |

### 1.3 注册回调

在 `agent_init()` 之后调用 `agent_set_callbacks` 注册：

```c
agent_set_callbacks(tts_cb, my_servo_tool_call_cb);
```

注册后，agent 主循环（`kernel.c:624`）遇到 LLM 下发 `robot.*` 工具时，会直接调用该回调，**不再走 agent 内部的 `CrabRuntimeCallTool`**。

### 1.4 交互时序

```
用户语音 → ASR → agent_input(text)
                    ↓
              agent 构造 system + messages + tools JSON
                    ↓
              LLM (POST /v1/chat/completions, tools=[robot.move/speak/stop,...])
                    ↓
              LLM 响应 tool_calls: [{name:"robot.move", arguments:"{...}"}]
                    ↓
              agent 调 your_tool_call_cb("robot.move", "{...}", result, size)
                    ↓
              舵机解析 args_json → 驱动硬件 → 写 result
                    ↓
              agent 把 result 作为 role:"tool" 消息追加进 messages
                    ↓
              下一轮 LLM（直到 LLM 不再请求工具，输出最终回复）
                    ↓
              tts_cb(final_text)
```

---

## 2. 工具协议详解

### 2.1 `robot.move` — 移动

让机器人向指定方向移动指定步数。

**参数 JSON**

| 字段 | 类型 | 必填 | 取值范围 | 说明 |
|---|---|---|---|---|
| `direction` | string | 是 | `forward` / `backward` / `left` / `right` | 移动方向（前/后/左/右） |
| `steps` | integer | 否 | `1` ~ `10` | 移动步数，默认按舵机实现（agent 侧声明 1~10） |

**LLM 下发示例**

```json
{
  "direction": "forward",
  "steps": 2
}
```

```json
{
  "direction": "left",
  "steps": 1
}
```

**下游解析示例**

```c
if (strcmp(name, "robot.move") == 0) {
    /* 用任意 JSON 解析器从 args_json 提取 direction / steps */
    char direction[16] = {0};
    int steps = 1;
    /* 例: 简单字符串匹配 (推荐用 LjParse 或 cJSON) */
    parse_direction(args_json, direction, sizeof direction);
    parse_int(args_json, "steps", &steps);

    /* 驱动舵机执行移动 */
    int rc = servo_move(direction, steps);

    /* 写结果 (建议 JSON, 便于 LLM 理解) */
    snprintf(result, result_max,
             "{\"success\":%s,\"action\":\"move\",\"direction\":\"%s\",\"steps\":%d}",
             rc == 0 ? "true" : "false", direction, steps);
    return rc;
}
```

**结果示例（成功）**

```json
{"success":true,"action":"move","direction":"forward","steps":2}
```

**结果示例（失败）**

```json
{"success":false,"action":"move","direction":"forward","steps":2,"error":"obstacle_detected"}
```

---

### 2.2 `robot.speak` — 语音播报

让机器人通过扬声器播报一段文本。

**参数 JSON**

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `text` | string | 是 | 需要播报的文本内容 |

**LLM 下发示例**

```json
{
  "text": "你好，我是机器人小乐"
}
```

```json
{
  "text": "移动完成，已到达目的地"
}
```

**下游解析示例**

```c
if (strcmp(name, "robot.speak") == 0) {
    char text[256] = {0};
    parse_string(args_json, "text", text, sizeof text);

    int rc = tts_speak(text);   /* 调 TTS/扬声器模块 */

    snprintf(result, result_max,
             "{\"success\":%s,\"action\":\"speak\",\"text\":\"%s\"}",
             rc == 0 ? "true" : "false", text);
    return rc;
}
```

**结果示例（成功）**

```json
{"success":true,"action":"speak","text":"你好，我是机器人小乐"}
```

---

### 2.3 `robot.stop` — 停止动作

停止机器人当前正在执行的动作（移动/播报等）。

**参数 JSON**

无参数，args_json 通常为 `"{}"`。

**LLM 下发示例**

```json
{}
```

**下游解析示例**

```c
if (strcmp(name, "robot.stop") == 0) {
    int rc = servo_stop_all();   /* 停止所有舵机/播放 */
    snprintf(result, result_max,
             "{\"success\":%s,\"action\":\"stop\"}",
             rc == 0 ? "true" : "false");
    return rc;
}
```

**结果示例（成功）**

```json
{"success":true,"action":"stop"}
```

---

## 3. 完整下游适配示例

下面是一个完整的舵机适配回调实现，可直接作为下游接入参考：

```c
/* servo_tool_call.c — 舵机侧 tool_call 适配实现 */
#include "agent/agent.h"
#include <stdio.h>
#include <string.h>

/* 简易 JSON 字符串提取 (生产环境建议用 LjParse/cJSON) */
static int extract_string(const char* json, const char* key,
                          char* out, size_t out_size) {
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"%s\":\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p + 1)) p++;  /* 简单处理转义 */
        out[i++] = *p++;
    }
    out[i] = 0;
    return 0;
}

static int extract_int(const char* json, const char* key, int* out) {
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"%s\":", key);
    const char* p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') p++;
    *out = atoi(p);
    return 0;
}

/* 舵机 tool_call 回调主入口 */
int on_servo_tool_call(const char* name, const char* args_json,
                       char* result, size_t result_max) {
    printf("[servo] >>> cmd=%s args=%s\n", name, args_json ? args_json : "{}");

    int rc = -1;

    if (strcmp(name, "robot.move") == 0) {
        char direction[16] = "forward";
        int steps = 1;
        extract_string(args_json, "direction", direction, sizeof direction);
        extract_int(args_json, "steps", &steps);
        if (steps < 1) steps = 1;
        if (steps > 10) steps = 10;

        /* TODO: 调实际舵机驱动接口 */
        rc = servo_move(direction, steps);
        snprintf(result, result_max,
                 "{\"success\":%s,\"action\":\"move\",\"direction\":\"%s\",\"steps\":%d}",
                 rc == 0 ? "true" : "false", direction, steps);

    } else if (strcmp(name, "robot.speak") == 0) {
        char text[256] = "";
        extract_string(args_json, "text", text, sizeof text);

        /* TODO: 调实际 TTS 接口 */
        rc = tts_speak(text);
        snprintf(result, result_max,
                 "{\"success\":%s,\"action\":\"speak\",\"text\":\"%s\"}",
                 rc == 0 ? "true" : "false", text);

    } else if (strcmp(name, "robot.stop") == 0) {
        /* TODO: 调实际停止接口 */
        rc = servo_stop_all();
        snprintf(result, result_max,
                 "{\"success\":%s,\"action\":\"stop\"}",
                 rc == 0 ? "true" : "false");

    } else {
        snprintf(result, result_max,
                 "{\"success\":false,\"error\":\"unknown tool: %s\"}", name);
        return -1;
    }

    printf("[servo] <<< result=%s rc=%d\n", result, rc);
    return rc;
}

/* TTS 回调 (agent 最终文本回复) */
void on_tts_output(const char* text) {
    printf("[tts] reply: %s\n", text ? text : "(null)");
    /* TODO: 调 TTS 接口 */
}
```

**注册到 agent**

```c
int main(int argc, char* argv[]) {
    AgentConfig cfg = {
        .llm_base_url = "https://api.example.com/v1/chat/completions",
        .llm_api_key  = "sk-xxx",
        .llm_model    = "deepseek-ai/DeepSeek-V4-Flash",
        .workspace_root = "/userdata/agent",
    };

    if (agent_init(&cfg) != 0) return 1;

    /* 注册舵机回调 */
    agent_set_callbacks(on_tts_output, on_servo_tool_call);

    /* ASR 识别文本喂给 agent */
    agent_input("向前走两步然后说你好");
    agent_deinit();
    return 0;
}
```

---

## 4. 协议约束与注意事项

### 4.1 参数来源

- 所有 `args_json` 字段名严格按本协议定义（`direction` / `steps` / `text`）
- 字段值为 LLM 生成，下游**必须做边界校验**（enum 合法性、步数范围、文本长度等）
- 未声明可选字段时，下游应使用合理默认值

### 4.2 返回值约定

- `return 0` — agent 视为成功，`result` 内容会原样回传给 LLM
- `return 非0` — agent 视为失败，会在 `result` 里追加 `"tool execution failed: N"` 标记
- `result` 内容建议为 JSON，便于 LLM 在下一轮正确理解执行状态
- `result` 写入长度**不得超过** `result_max`（否则会被截断）

### 4.3 并发与阻塞

- 工具定义中 `concurrency = CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE`，表示 agent 不会并发下发多个 robot 工具
- 但下游回调内部**不应长时间阻塞**（如等待机械动作完全完成才 return），否则会拖长 LLM 对话轮次。建议：
  - 立即启动动作并 return（异步执行）
  - 或在合理超时内同步等待（如单步移动 ≤1s）
- 如动作耗时较长，可在 `result` 中返回 `"status":"started"` 让 LLM 知道动作已启动

### 4.4 LLM 可能的调用模式

LLM 一次响应可能下发**多个工具调用**（agent 会串行调用回调）：

```json
"tool_calls": [
  {"name":"robot.move",  "arguments":"{\"direction\":\"forward\",\"steps\":2}"},
  {"name":"robot.speak", "arguments":"{\"text\":\"已到达\"}"}
]
```

下游需保证每次回调调用都是独立可执行的。

### 4.5 工具不在协议中时

若 LLM 误下发非 `robot.*` 工具（理论上不会发生，因为发给 LLM 的 tools schema 只含已注册工具），下游应返回：

```json
{"success":false,"error":"unknown tool: <name>"}
```

并 `return -1`。

---

## 5. 协议字段速查表

| 工具 | direction | steps | text |
|---|---|---|---|
| `robot.move` | 必填，enum 4 值 | 可选，1~10 | — |
| `robot.speak` | — | — | 必填，string |
| `robot.stop` | — | — | — |

**direction enum 合法值**：`forward` / `backward` / `left` / `right`

---

## 6. 相关代码位置

| 文件 | 行 | 说明 |
|---|---|---|
| `agent_core.c` | 30-48 | 三个 robot 工具的 `CrabToolSpec` 定义 |
| `agent_core.c` | 88-93 | 注册 robot 工具到 runtime.registry |
| `include/litecrab/kernel.h` | 217-219 | `AgentToolCallCallback` 类型与注册接口 |
| `include/agent/agent.h` | 25-35 | `agent_tool_call_callback_fn` 类型与 `agent_set_callbacks` |
| `src/kernel/kernel.c` | 624-637 | 主循环里调用回调的逻辑 |
| `src/kernel/kernel.c` | 379-388 | 渲染 tools JSON 发给 LLM |
| `src/kernel/llm.c` | 162-218 | 从 LLM 响应解析 tool_calls |
| `agent_test.c` | 13-36 | 完整下游适配参考实现 |
