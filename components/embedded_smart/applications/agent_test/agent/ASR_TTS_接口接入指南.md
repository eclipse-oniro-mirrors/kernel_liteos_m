# Agent ASR / TTS 接口接入指南

本文档说明 `@openharmony/agent` 组件对 ASR（语音识别）和 TTS（语音合成）模块预留的接入接口，以及外部模块如何对接。

---

## 1. 总体架构

Agent 不内建 ASR / TTS 引擎，仅对外预留 C 语言回调式接口。外部 ASR / TTS 引擎自行实现后通过以下函数注入：

```
        (识别文本)
 ASR ─────────────►  agent_input()  ─►  [LLM Tool Loop]  ─►  agent_tts_cb(text)  ─►  TTS
                                              │
                                              └────────────►  agent_tool_call_cb()  ─►  舵机/动作执行器
```

- **ASR 侧**：单向输入，外部 ASR 把识别出的文本字符串喂给 `agent_input`。Agent 不接收音频流。
- **TTS 侧**：单向输出，Agent 把最终自然语言回复文本通过 `agent_tts_callback_fn` 回调送出，外部 TTS 自行合成播放。
- **工具调用侧**：LLM 调用 `robot.move` / `robot.speak` / `robot.stop` 等工具时，走 `agent_tool_call_callback_fn` 回调（与 TTS 是两条独立路径，不要混淆）。

---

## 2. 相关文件位置

| 类型 | 文件路径 |
|---|---|
| 公共头文件（接口声明） | `device/soc/hisilicon/common/platform/agent/include/agent/agent.h` |
| 接口实现 | `device/soc/hisilicon/common/platform/agent/agent_core.c` |
| 示例程序 | `device/soc/hisilicon/common/platform/agent/agent_test.c` |
| 组件描述 | `device/soc/hisilicon/common/platform/agent/bundle.json` |
| 构建脚本 | `device/soc/hisilicon/common/platform/agent/BUILD.gn` |

---

## 3. 接口详解

### 3.1 ASR 入口 —— `agent_input`

**声明**（`agent.h:43`）：
```c
/* 输入文本 (ASR 模块把识别文本通过这个喂给 agent, 触发 LLM tool 循环) */
int agent_input(const char* text);
```

**实现**（`agent_core.c:135`）：内部构造一条 `LITE_MSG_CHAT` 消息，调用 `run_round()` 与 LLM 完成一轮 tool 循环；最终自然语言回复通过 TTS 回调输出，工具调用通过 tool_call 回调输出。

| 参数 | 含义 |
|---|---|
| `text` | ASR 模块识别出的字符串（UTF-8，以 `\0` 结尾），不可为 NULL |
| 返回值 | 0 成功；-1 失败（未初始化 / text 为空 / LLM 返回错误） |

**调用约束**：
- 必须先调用 `agent_init()` 完成初始化。
- 该函数是**同步阻塞**调用，内部会等 LLM 整轮 tool 循环结束才返回。若 ASR 在独立线程采集，建议在 ASR 线程中以队列方式串行喂入，避免并发调用。
- 一次 `agent_input` 对应一次完整对话轮次，自带 `requestId` 和 `sessionId="default"`。

---

### 3.2 TTS 输出回调 —— `agent_tts_callback_fn`

**声明**（`agent.h:22`）：
```c
/* TTS 文本输出回调 (agent 最终回复文本通过这个回调输出给 TTS 模块) */
typedef void (*agent_tts_callback_fn)(const char* text);
```

| 参数 | 含义 |
|---|---|
| `text` | Agent 最终自然语言回复文本，UTF-8，回调返回后 Agent 会 `free` 该内存，**实现方不要保存指针**，如需保留请自行 `strdup` |
| 返回值 | 无 |

**触发时机**：`agent_core.c:163-164`，`run_round` 返回非空且非 `"ERROR:"` 开头时调用。
**线程上下文**：在与 `agent_input` 相同的调用线程内被回调。

**注意：这是 LLM 的自然语言回复，不是 `robot.speak` 工具调用。**

---

### 3.3 工具调用回调 —— `agent_tool_call_callback_fn`

**声明**（`agent.h:25`）：
```c
typedef int (*agent_tool_call_callback_fn)(const char* name,
                                           const char* args_json,
                                           char* result,
                                           size_t result_max);
```

| 参数 | 含义 |
|---|---|
| `name` | 工具名，当前内置三个：`robot.move` / `robot.speak` / `robot.stop` |
| `args_json` | LLM 给出的参数 JSON 字符串，例如 `{"direction":"forward","steps":2}` |
| `result` | 实现方写入执行结果 JSON，回传给 LLM 用于后续推理 |
| `result_max` | `result` 缓冲区最大字节数 |
| 返回值 | 0 成功；非 0 失败 |

**`robot.speak` 与 `agent_tts_callback_fn` 的区别**：
- `robot.speak` 是 LLM 主动调用"让机器人开口说话"的工具，参数是 `text`，由舵机/播放器执行（如离线 TTS、预录语音）。走 `on_tool_call`。
- `agent_tts_callback_fn` 是 LLM 这一轮回复给用户的自然语言文本。走 `on_tts`。

通常接入在线 TTS 引擎实现 `on_tts`；离线播报词库实现 `robot.speak` 的处理。

---

### 3.4 回调注册 —— `agent_set_callbacks`

**声明**（`agent.h:34`）：
```c
void agent_set_callbacks(agent_tts_callback_fn tts_cb,
                         agent_tool_call_callback_fn tool_call_cb);
```

**实现**（`agent_core.c:108`）：保存 TTS 回调到 `g_tts_cb`，工具回调透传给 `AgentLoopSetToolCallCallback`。

- 两个参数都允许为 NULL（对应回调不触发）。
- 建议在 `agent_init()` 成功后、`agent_input()` 之前调用。

---

### 3.5 初始化 —— `agent_init`

**声明**（`agent.h:31`）：
```c
int agent_init(const AgentConfig* cfg);
```

`AgentConfig` 字段（`agent.h:11`）：

| 字段 | 说明 |
|---|---|
| `llm_base_url` | LLM 接口地址，如 `https://api.siliconflow.cn/v1/chat/completions` |
| `llm_api_key` | API Key |
| `llm_model` | 模型名，如 `deepseek-ai/DeepSeek-V4-Flash` |
| `llm_timeout_ms` | 单次请求超时，0 走默认 60000ms |
| `max_tool_iterations` | 最大工具循环次数 |
| `conversation_history_limit` | 对话历史保留条数 |
| `workspace_root` | 工作目录，NULL 走默认 `/userdata/agent` |

返回 0 成功，-1 失败。

---

### 3.6 销毁 —— `agent_deinit`

```c
void agent_deinit(void);
```
停止 agent 线程、销毁消息总线，重置初始化标志。

---

## 4. 完整接入示例

### 4.1 最小可运行示例（文本 IO，来自 `agent_test.c`）

```c
#include "agent/agent.h"
#include <stdio.h>
#include <string.h>

/* TTS 回调：把 agent 回复文本送给你的 TTS 引擎 */
static void on_tts(const char* text) {
    printf("[tts] >>> reply: %s\n", text ? text : "(null)");
    /* TODO: MyTTS_SynthesizeAndPlay(text); */
}

/* 工具调用回调：处理机器人动作 */
static int on_tool_call(const char* name, const char* args_json,
                        char* result, size_t result_max) {
    if (strcmp(name, "robot.move") == 0) {
        /* TODO: 解析 args_json 取 direction/steps, 驱动舵机 */
        snprintf(result, result_max,
                 "{\"success\":true,\"action\":\"move\",\"args\":%s}",
                 args_json ? args_json : "{}");
    } else if (strcmp(name, "robot.speak") == 0) {
        /* TODO: 取 text 走离线 TTS / 预录词库播放 */
        snprintf(result, result_max, "{\"success\":true,\"action\":\"speak\"}");
    } else if (strcmp(name, "robot.stop") == 0) {
        snprintf(result, result_max, "{\"success\":true,\"action\":\"stop\"}");
    } else {
        snprintf(result, result_max, "{\"success\":false,\"error\":\"unknown:%s\"}", name);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) { printf("usage: %s <text>\n", argv[0]); return 1; }

    AgentConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.llm_base_url = "https://47.102.37.23/v1/chat/completions";
    cfg.llm_api_key  = "sk-xxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    cfg.llm_model    = "deepseek-ai/DeepSeek-V4-Flash";
    cfg.llm_timeout_ms         = 60000;
    cfg.max_tool_iterations    = 5;
    cfg.conversation_history_limit = 10;
    cfg.workspace_root = "/userdata/agent";

    if (agent_init(&cfg) != 0) { printf("init failed\n"); return 1; }
    agent_set_callbacks(on_tts, on_tool_call);

    /* 这里把 argv[1] 当作 ASR 识别结果喂进来 */
    agent_input(argv[1]);

    agent_deinit();
    return 0;
}
```

构建：`agent_test` 已在 `BUILD.gn:42` 定义，`hb build` 时随 `agent_group` 一起产出。

---

### 4.2 对接真实 ASR 引擎

ASR 引擎通常以独立线程持续采集音频并识别。典型对接模式：

```c
/* 1) ASR 线程：识别出一条文本后串行喂入 */
void asr_thread(void) {
    char buf[512];
    while (asr_get_text(buf, sizeof buf) > 0) {  /* 你自己的 ASR 接口 */
        /* 串行化：上一轮 agent_input 返回前不要再次喂入 */
        agent_input(buf);
    }
}

/* 2) 主线程：初始化 + 注册回调 */
int main(void) {
    AgentConfig cfg = { ... };
    agent_init(&cfg);
    agent_set_callbacks(on_tts, on_tool_call);

    start_asr_thread();   /* 启动 ASR 采集线程 */
    /* 主线程可阻塞等待退出信号 */
    wait_for_shutdown();

    agent_deinit();
    return 0;
}
```

**关键点**：
- `agent_input` 是阻塞调用，ASR 线程应保证一次只喂一条，等返回后再喂下一条；若需非阻塞，可在 ASR 侧用生产者队列 + 单独的 dispatcher 线程。
- Agent **不接收音频流**，ASR 必须先输出文本。
- ASR 与 TTS 引擎的初始化/销毁由调用方自行管理，与 `agent_init` / `agent_deinit` 无依赖关系。

---

### 4.3 对接真实 TTS 引擎

```c
extern int MyTTS_Init(const char* model_path);
extern int MyTTS_SynthesizeAndPlay(const char* text);  /* 同步播放 */
extern void MyTTS_Deinit(void);

static void on_tts(const char* text) {
    if (!text) return;
    /* Agent 返回后立即 free 该指针，这里在回调内同步播放即可；
     * 若 TTS 耗时长，建议 strup 后丢到 TTS 线程播放，避免阻塞 agent_input 返回 */
    char* dup = strdup(text);
    if (dup) {
        MyTTS_SynthesizeAndPlay(dup);
        free(dup);
    }
}

int main(void) {
    MyTTS_Init("/userdata/tts/model.bin");

    AgentConfig cfg = { ... };
    agent_init(&cfg);
    agent_set_callbacks(on_tts, on_tool_call);

    /* ... ASR 喂入循环 ... */

    agent_deinit();
    MyTTS_Deinit();
    return 0;
}
```

**关键点**：
- `on_tts` 在 `agent_input` 所在线程内同步回调；若 TTS 合成耗时较长，建议在回调内 `strdup` 文本后投递到独立 TTS 播放线程，避免拖慢 LLM 循环。
- 回调返回后 Agent 会 `free` 原文本指针，**不要保存原指针**。

---

## 5. 常见问题

**Q1: `robot.speak` 工具调用的文本会走 `on_tts` 吗？**
不会。`robot.speak` 走 `on_tool_call` 回调，由调用方在工具执行侧播放；`on_tts` 只承载 LLM 的自然语言回复文本。

**Q2: 可以并发多次调用 `agent_input` 吗？**
不可以。当前实现无锁，并发调用会破坏对话状态。请串行喂入。

**Q3: ASR 必须输出文本吗？支持音频流？**
只支持文本。Agent 没有音频处理能力，ASR 必须先把音频转成文字。

**Q4: TTS 回调里的 text 谁负责释放？**
Agent 负责释放。回调实现方只需读取即可；若要异步播放，必须自行 `strdup`。

**Q5: 如何让 LLM 能调用我自定义的工具？**
使用 `agent_register_tool(name, description, parameters_json)`（`agent.h:38`）注册。注意当前实现未解析 `parameters_json`（`agent_core.c:131` 标注 TODO），如需完整参数校验需自行扩展。

**Q6: `agent_input` 返回 -1 但没看到错误信息？**
检查：1) 是否先调用了 `agent_init` 且返回 0；2) `text` 是否为 NULL；3) 内部 `run_round` 返回是否以 `"ERROR:"` 开头（参考 `agent_core.c:159`）。

---

## 6. 接口速查表

| 接口 | 声明位置 | 实现位置 | 作用 |
|---|---|---|---|
| `agent_init` | `agent.h:31` | `agent_core.c:50` | 初始化 agent |
| `agent_set_callbacks` | `agent.h:34` | `agent_core.c:108` | 注册 TTS / 工具回调 |
| `agent_tts_callback_fn` | `agent.h:22` | `agent_core.c:18,163` | TTS 文本输出回调类型 |
| `agent_tool_call_callback_fn` | `agent.h:25` | 透传给 `AgentLoopSetToolCallCallback` | 工具调用回调类型 |
| `agent_register_tool` | `agent.h:38` | `agent_core.c:114` | 注册自定义工具 |
| `agent_input` | `agent.h:43` | `agent_core.c:135` | ASR 文本输入入口 |
| `agent_deinit` | `agent.h:46` | `agent_core.c:172` | 销毁 agent |
