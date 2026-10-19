/* agent_core.c — agent 核心实现 (替代 main.c)
 * 提供 agent_init / agent_input / agent_set_callbacks / agent_set_stream_callbacks
 * / agent_register_tool, 内部调用 LiteCrab kernel 的 AgentLoopInit/Start + MessageBus
 *
 * 流式(SPEC_AGENT_ADAPTATION 修改点①, 2026-10-15): agent_set_stream_callbacks
 * 注册增量回调后, LLM 调用走 SSE 流式(引擎 llm.c 的 LlmSetTextDeltaHook →
 * llm_chat_stream_raw), 正文按 delta 实时送出; 全文 tts_cb 不再触发。
 *
 * 注: 机器人工具 (robot.move/speak/stop/action/pose/play_sound) 的定义和注册
 *     已迁移到 src/runtime/robot_tools.c, 由 AgentLoopInit 内部统一注册。
 *     本文件不再硬编码任何 CrabToolSpec。
 */
#include "agent/agent.h"
#include "litecrab/kernel.h"
#include "litecrab/hub.h"
#include "litecrab/runtime.h"
#include "litecrab/memory.h"
#include "litecrab/observability.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static agent_tts_callback_fn g_tts_cb = NULL;
static agent_text_delta_fn g_delta_cb = NULL;   /* 流式增量回调(修改点①) */
static void* g_delta_ud = NULL;
static int g_initialized = 0;

/* 通用桩 handler (用于 agent_register_tool 动态注册的工具)
 * 设置了 tool_call_cb 后, kernel.c 不调此 handler, 而是调回调 */
static int stub_execute(CrabRuntime* r, const CrabToolCall* c, CrabRawResult* raw) {
    (void)r; (void)c;
    CrabRawResultSetStdout(raw, "tool relayed to external executor");
    return 0;
}

int agent_init(const AgentConfig* cfg) {
    if (!cfg || !cfg->llm_base_url || !cfg->llm_api_key || g_initialized)
        return -1;

    /* 创建 workspace 目录 */
    const char* ws = cfg->workspace_root ? cfg->workspace_root : "/userdata/agent";
    mkdir(ws, 0755);

    /* 构造 LlmConfig */
    LlmConfig llm_cfg;
    memset(&llm_cfg, 0, sizeof llm_cfg);
    llm_cfg.baseUrl = cfg->llm_base_url;
    llm_cfg.apiKey = cfg->llm_api_key;
    llm_cfg.model = cfg->llm_model;
    llm_cfg.timeoutMs = cfg->llm_timeout_ms ? cfg->llm_timeout_ms : 60000;
    llm_cfg.maxTokens = 4096;
    llm_cfg.temperature = 0.7;
    llm_cfg.stream = 0;   /* 已不生效: 流式与否由 delta 钩子(LlmSetTextDeltaHook)决定 */
    /* vhost 用于 SNI/Host 头 (域名), baseUrl 用 IP 避免 DNS 崩溃 */
    snprintf(llm_cfg.vhost, sizeof llm_cfg.vhost, "api.siliconflow.cn");

    /* 构造 CrabRuntimeConfig */
    CrabRuntimeConfig rt_cfg;
    memset(&rt_cfg, 0, sizeof rt_cfg);
    rt_cfg.workspaceRoot = ws;
    rt_cfg.maxResponseBytes = 65536;
    rt_cfg.maxRawBytes = 65536;
    rt_cfg.maxWarnings = 8;

    /* 构造 MemoryConfig (默认值) */
    MemoryConfig mem_cfg;
    MemoryConfigDefaults(&mem_cfg);

    /* 初始化 — AgentLoopInit 内部会注册:
     *   - builtin tools (skill_read/skill_complete)
     *   - memory tool (memory_remember, 若 memory 启用)
     *   - robot tools (robot.move/speak/stop/action/pose/play_sound)
     */
    MessageBusInit();
    if (AgentLoopInit(&rt_cfg, &llm_cfg, &mem_cfg))
        return -1;

    /* 刷新工具目录 */
    if (AgentLoopRefreshToolCatalog())
        return -1;

    /* 启动 agent 线程 */
    if (AgentLoopStart())
        return -1;

    g_initialized = 1;
    printf("[agent] init ok, robot_tools=%zu robot_actions=%zu\n",
           CrabRobotToolsCount(), CrabRobotActionCount());
    return 0;
}

void agent_set_callbacks(agent_tts_callback_fn tts_cb,
                         agent_tool_call_callback_fn tool_call_cb) {
    g_tts_cb = tts_cb;
    AgentLoopSetToolCallCallback(tool_call_cb);
    /* 全文模式: 注销流式钩子(与 agent_set_stream_callbacks 互斥, 后设者生效) */
    g_delta_cb = NULL;
    g_delta_ud = NULL;
    LlmSetTextDeltaHook(NULL, NULL);
}

/* 签名适配: 引擎钩子是 (ud, delta, len), agent 回调是 (delta, len, ud) */
static int agent_delta_shim(void* ud, const char* delta, int len) {
    (void)ud;
    return g_delta_cb(delta, len, g_delta_ud);
}

void agent_set_stream_callbacks(agent_text_delta_fn delta_cb, void* delta_ud,
                                agent_tool_call_callback_fn tool_call_cb) {
    AgentLoopSetToolCallCallback(tool_call_cb);
    if (delta_cb == NULL) {
        /* NULL 增量 = 退回全文模式(tts_cb 保持现值) */
        g_delta_cb = NULL;
        g_delta_ud = NULL;
        LlmSetTextDeltaHook(NULL, NULL);
        return;
    }
    g_delta_cb = delta_cb;
    g_delta_ud = delta_ud;
    LlmSetTextDeltaHook(agent_delta_shim, NULL);
}

int agent_register_tool(const char* name, const char* description,
                        const char* parameters_json) {
    if (!g_initialized || !name || !description)
        return -1;
    /* 简化实现: 用桩 handler 注册工具规格
     * 完整实现需要解析 parameters_json 得到 CrabToolParamSpec */
    CrabRuntime* rt = AgentLoopRuntime();
    if (!rt)
        return -1;
    CrabToolSpec spec;
    memset(&spec, 0, sizeof spec);
    spec.name = name;
    spec.description = description;
    spec.enabledByDefault = 1;
    spec.readonly = 1;
    spec.execute = stub_execute;
    spec.filter = CrabBasicFilter;
    /* TODO: 解析 parameters_json 填 spec.params */
    return CrabRegistryRegister(&rt->registry, &spec);
}

int agent_input(const char* text) {
    if (!text || !g_initialized)
        return -1;

    printf("[agent] calling run_round directly\n"); fflush(stdout);

    /* 直接调 run_round, 绕过消息总线的 RequestShouldStop 问题 */
    LiteMsg dummy = {0};
    dummy.type = LITE_MSG_CHAT;
    dummy.priority = LITE_PRIORITY_NORMAL;
    dummy.replyMode = LITE_REPLY_ACK_ONLY;
    /* 2026-10-19 修复: 60s 死线在长故事轮必爆——LLM 流式 45s + TTS 排干等播放
     * 80s+(工具执行排在旁白播完之后), 合计 ~130s 时 RequestShouldStop 已真,
     * kernel.c 工具环跳过已决定的调用(板测: 结尾念了挥手, calls=1 却没投递,
     * "tool cancelled: request deadline reached")。voice 的真取消路径是
     * barge-in delta 中止/flow 退出, 这里只是兜底跑飞守卫, 放宽到 5 分钟。 */
    dummy.deadlineMs = LiteMonotonicMs() + 300000;
    snprintf(dummy.requestId, sizeof dummy.requestId, "req_%lld", (long long)time(NULL));
    snprintf(dummy.sessionId, sizeof dummy.sessionId, "default");

    SkillRouterResult route = {0};
    route.action = SKILL_ROUTE_BASE;
    AgentTraceScope scope = {0};
    AgentTraceStart(&scope, "", "default", text);
    LlmRequestScopeBegin(dummy.deadlineMs, 8);

    char* ans = run_round(text, "default", dummy.requestId, &route, &scope, &dummy);

    LlmRequestScopeEnd();
    AgentTraceFinish(&scope, ans && strncmp(ans, "ERROR:", 6) ? "ok" : "error", ans);

    printf("[agent] run_round returned: %s\n", ans ? ans : "(null)"); fflush(stdout);

    /* 流式模式(修改点①): 正文已按增量实时交付, 全文 tts_cb 不再走;
     * 全文模式(未设 delta 钩子)保持原行为。 */
    if (g_tts_cb && ans && g_delta_cb == NULL)
        g_tts_cb(ans);

    if (ans)
        free(ans);

    return ans ? 0 : -1;
}

void agent_deinit(void) {
    if (!g_initialized)
        return;
    AgentLoopStop();
    MessageBusDestroy();
    g_initialized = 0;
}
