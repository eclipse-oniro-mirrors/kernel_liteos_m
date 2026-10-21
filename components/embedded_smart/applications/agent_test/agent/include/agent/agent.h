#ifndef AGENT_H
#define AGENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* agent 配置 */
typedef struct {
    const char* llm_base_url;     /* 如 "https://api.siliconflow.cn/v1/chat/completions" */
    const char* llm_api_key;
    const char* llm_model;
    int llm_timeout_ms;
    int max_tool_iterations;
    int conversation_history_limit;
    const char* workspace_root;
} AgentConfig;

/* TTS 文本输出回调 (agent 最终回复文本通过这个回调输出给 TTS 模块) */
typedef void (*agent_tts_callback_fn)(const char* text);

/* tool_call 输出回调 (agent 把 LLM 返回的工具调用通过回调输出给舵机模块) */
typedef int (*agent_tool_call_callback_fn)(const char* name,
                                           const char* args_json,
                                           char* result,
                                           size_t result_max);

/* 初始化 agent */
int agent_init(const AgentConfig* cfg);

/* 设置输出回调 */
void agent_set_callbacks(agent_tts_callback_fn tts_cb,
                         agent_tool_call_callback_fn tool_call_cb);

/* 流式文本增量回调(SPEC_AGENT_ADAPTATION 修改点①, 2026-10-15 参考实现):
 * 每个正文 delta(UTF-8, 已反转义, 思考过程不透传)同步回调一次; 每轮流正文
 * 结束(含中止/出错)补调一次(delta=NULL, len=0) — 调用方据此收尾(如冲尾句、
 * 结束 TTS 流任务)。回调在 agent_input 调用线程内同步执行, 阻塞=背压。
 * 返回 0 继续; 非 0 = 中止生成(播放期打断场景, agent 尽快作废本轮)。 */
typedef int (*agent_text_delta_fn)(const char* delta, int len, void* ud);

/* 流式版回调注册: 设置后 agent 的 LLM 调用走 SSE 流式(内部 llm_chat_stream_raw),
 * 正文按增量实时送出; 最终回复不再走 agent_set_callbacks 的全文 tts_cb
 * (增量已实时交付)。tool_call_cb 语义同 agent_set_callbacks。
 * 与 agent_set_callbacks 互斥(后设者生效): agent_set_callbacks 会注销流式钩子,
 * 退回"全文 tts_cb"模式 — 旧调用方行为不变。 */
void agent_set_stream_callbacks(agent_text_delta_fn delta_cb, void* delta_ud,
                                agent_tool_call_callback_fn tool_call_cb);

/* 注册工具规格 (供 agent 生成 OpenAI tools JSON 发给 LLM) */
int agent_register_tool(const char* name,
                        const char* description,
                        const char* parameters_json);

/* 输入文本 (ASR 模块把识别文本通过这个喂给 agent, 触发 LLM tool 循环) */
int agent_input(const char* text);

/* 销毁 agent */
void agent_deinit(void);

#ifdef __cplusplus
}
#endif
#endif /* AGENT_H */
