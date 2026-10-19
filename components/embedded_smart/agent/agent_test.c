/* agent_test.c — agent 机器人指令测试程序
 * 用法:
 *   agent_test --dump-tools                打印所有已注册工具的 JSON schema (不调 LLM)
 *   agent_test <文本输入>                  触发 LLM tool 循环 + 机器人指令发送
 * 示例:
 *   agent_test --dump-tools
 *   agent_test hello
 *   agent_test "walk forward 2 steps"
 *   agent_test "stop"
 *   agent_test "jump three times"
 *   agent_test "wave your right hand"
 *   agent_test "raise both hands"
 *   agent_test "sit down"
 *   agent_test "show me a performance"
 */
#include "agent/agent.h"
#include "litecrab/runtime.h"
#include "litecrab/kernel.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int g_tool_call_count = 0;

/* 通用 JSON 字段提取 (轻量, 不需要 cJSON/LjParse)
 * 仅用于测试回调里把 args_json 关键字段回显到 result, 不是生产代码
 * 容忍冒号前后空格 (LLM 下发可能带空格: "key": "value" 或 "key" : "value")
 * 匹配失败时保留 out 的原值 (默认值), 不清空 */
static void extract_string_field(const char* json, const char* key,
                                 char* out, size_t out_size) {
    if (!json || !key || !out || out_size == 0)
        return;  /* 不清空 out, 保留调用者设的默认值 */
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"%s\"", key);  /* 只匹配 "key" */
    const char* p = strstr(json, pattern);
    if (!p)
        return;  /* 没找到 key, 保留默认值 */
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t')  /* 跳过 key 后空格 */
        p++;
    if (*p != ':')
        return;  /* 不是冒号, 保留默认值 */
    p++;
    while (*p == ' ' || *p == '\t')  /* 跳过冒号后空格 */
        p++;
    if (*p != '"')
        return;  /* 不是字符串值, 保留默认值 */
    p++;  /* 跳过开头引号 */
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p + 1))
            p++;
        out[i++] = *p++;
    }
    out[i] = 0;
}

static int extract_int_field(const char* json, const char* key, int default_value) {
    if (!json || !key)
        return default_value;
    char pattern[64];
    snprintf(pattern, sizeof pattern, "\"%s\"", key);  /* 只匹配 "key" */
    const char* p = strstr(json, pattern);
    if (!p)
        return default_value;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t')  /* 跳过 key 后空格 */
        p++;
    if (*p != ':')
        return default_value;
    p++;
    while (*p == ' ' || *p == '\t')  /* 跳过冒号后空格 */
        p++;
    if (*p != '-' && (*p < '0' || *p > '9'))
        return default_value;
    return atoi(p);
}

/* tool_call 回调 (模拟舵机模块, 收到命令打印 + 模拟执行)
 * 覆盖所有 4 个 robot.* 工具, 把关键字段回显到 result 给 LLM 看 */
static int on_tool_call(const char* name, const char* args_json,
                        char* result, size_t result_max) {
    g_tool_call_count++;
    printf("[servo #%d] >>> cmd=%s args=%s\n",
           g_tool_call_count, name, args_json ? args_json : "{}");

    if (strcmp(name, "robot.action") == 0) {
        char action[32] = "walk";
        int steps = 3, speed = 1000, direction = 1, amount = 25;
        extract_string_field(args_json, "action", action, sizeof action);
        steps = extract_int_field(args_json, "steps", 3);
        speed = extract_int_field(args_json, "speed", 1000);
        direction = extract_int_field(args_json, "direction", 1);
        amount = extract_int_field(args_json, "amount", 25);
        printf("[servo #%d]     -> action=%s steps=%d speed=%d dir=%d amount=%d\n",
               g_tool_call_count, action, steps, speed, direction, amount);
        snprintf(result, result_max,
                 "{\"success\":true,\"action\":\"%s\",\"steps\":%d,\"speed\":%d,"
                 "\"direction\":%d,\"amount\":%d}",
                 action, steps, speed, direction, amount);
    } else if (strcmp(name, "robot.pose") == 0) {
        int ll = extract_int_field(args_json, "left_leg", 90);
        int rl = extract_int_field(args_json, "right_leg", 90);
        int lf = extract_int_field(args_json, "left_foot", 90);
        int rf = extract_int_field(args_json, "right_foot", 90);
        int lh = extract_int_field(args_json, "left_hand", 45);
        int rh = extract_int_field(args_json, "right_hand", 135);
        int tm = extract_int_field(args_json, "time_ms", 700);
        printf("[servo #%d]     -> pose ll=%d rl=%d lf=%d rf=%d lh=%d rh=%d t=%dms\n",
               g_tool_call_count, ll, rl, lf, rf, lh, rh, tm);
        snprintf(result, result_max,
                 "{\"success\":true,\"action\":\"pose\",\"servos\":{"
                 "\"left_leg\":%d,\"right_leg\":%d,\"left_foot\":%d,\"right_foot\":%d,"
                 "\"left_hand\":%d,\"right_hand\":%d},\"time_ms\":%d}",
                 ll, rl, lf, rf, lh, rh, tm);
    } else if (strcmp(name, "robot.play_sound") == 0) {
        char sound[32] = "beep";
        int volume = 50;
        extract_string_field(args_json, "sound", sound, sizeof sound);
        volume = extract_int_field(args_json, "volume", 50);
        printf("[servo #%d]     -> play_sound=%s volume=%d\n",
               g_tool_call_count, sound, volume);
        snprintf(result, result_max,
                 "{\"success\":true,\"action\":\"play_sound\",\"sound\":\"%s\",\"volume\":%d}",
                 sound, volume);
    } else if (strcmp(name, "robot.stop") == 0) {
        printf("[servo #%d]     -> stop all motions\n", g_tool_call_count);
        snprintf(result, result_max, "{\"success\":true,\"action\":\"stop\"}");
    } else {
        printf("[servo #%d]     -> unknown tool: %s\n", g_tool_call_count, name);
        snprintf(result, result_max,
                 "{\"success\":false,\"error\":\"unknown tool: %s\"}", name);
    }

    printf("[servo #%d] <<< result=%s\n", g_tool_call_count, result);
    return 0;
}

/* TTS 回调 (模拟 TTS 模块, 收到文本打印) */
static void on_tts(const char* text) {
    printf("[tts] >>> reply: %s\n", text ? text : "(null)");
}

/* --dump-tools 模式: 不调 LLM, 只验证工具注册表 + schema 渲染
 * 用于在不依赖网络/LLM 的情况下验证:
 *   - 所有 robot.* 工具是否注册成功
 *   - tools JSON 是否能正常渲染
 *   - robot.action 的 enum 是否包含 23 个动作
 *   - 各工具参数 schema 是否完整 */
static int dump_tools_mode(void) {
    AgentConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.llm_base_url = "https://0.0.0.0/v1/chat/completions";
    cfg.llm_api_key = "dummy";
    cfg.llm_model = "dummy";
    cfg.llm_timeout_ms = 1000;
    cfg.workspace_root = "/userdata/agent";

    printf("=== dump-tools mode ===\n");
    printf("[init] CrabRobotToolsCount=%zu CrabRobotActionCount=%zu\n",
           CrabRobotToolsCount(), CrabRobotActionCount());

    /* agent_init 会注册所有工具并刷新 catalog */
    if (agent_init(&cfg) != 0) {
        printf("[FAIL] agent_init failed\n");
        return 1;
    }

    /* 取 catalog 并渲染 OpenAI tools JSON */
    CrabRuntime* rt = AgentLoopRuntime();
    if (!rt) {
        printf("[FAIL] AgentLoopRuntime returned NULL\n");
        agent_deinit();
        return 1;
    }
    CrabToolCatalogView cat;
    if (CrabRuntimeGetToolCatalog(rt, &cat)) {
        printf("[FAIL] CrabRuntimeGetToolCatalog failed\n");
        agent_deinit();
        return 1;
    }
    printf("[catalog] total tools registered: %zu\n", cat.toolCount);

    /* 列出所有工具名 */
    printf("[catalog] tool names:\n");
    for (size_t i = 0; i < cat.toolCount; i++) {
        const CrabToolSpec* t = &cat.tools[i];
        printf("  [%zu] %-20s  params=%zu  default_enabled=%d\n",
               i, t->name, t->paramCount, t->enabledByDefault);
    }

    /* 渲染为 OpenAI tools JSON */
    static char tools_json[65536];
    if (LlmToolAdapterRenderOpenaiToolsJson(&cat, tools_json, sizeof tools_json)) {
        printf("[FAIL] LlmToolAdapterRenderOpenaiToolsJson failed\n");
        agent_deinit();
        return 1;
    }
    printf("\n[tools JSON] (size=%zu bytes):\n%s\n\n", strlen(tools_json), tools_json);

    /* 校验关键工具存在 (robot.move/robot.speak 已删除, 不再注册)
     * 注: memory_remember 默认禁用 (MemoryConfig.enabled=0), 不在注册表里是正常的,
     *     所以不列入 expected, 只列为 optional */
    const char* expected[] = {
        "robot.action", "robot.pose", "robot.play_sound", "robot.stop",
        "skill_read", "skill_complete",
    };
    const char* optional[] = {"memory_remember"};
    int missing = 0;
    /* 同时校验已删除的工具确实不在注册表里 */
    const char* removed[] = {"robot.move", "robot.speak"};
    for (size_t i = 0; i < sizeof(removed) / sizeof(removed[0]); i++) {
        if (CrabRegistryFind(&rt->registry, removed[i])) {
            printf("[FAIL] tool \"%s\" should have been removed but is still registered\n", removed[i]);
            missing++;
        }
    }
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        if (!CrabRegistryFind(&rt->registry, expected[i])) {
            printf("[FAIL] missing expected tool: %s\n", expected[i]);
            missing++;
        }
    }
    /* memory_remember 是可选的 (memory 默认禁用时不在注册表里, 属正常) */
    for (size_t i = 0; i < sizeof(optional) / sizeof(optional[0]); i++) {
        if (CrabRegistryFind(&rt->registry, optional[i]))
            printf("[info] optional tool \"%s\" is registered (memory enabled)\n", optional[i]);
        else
            printf("[info] optional tool \"%s\" not registered (memory disabled, normal)\n", optional[i]);
    }
    /* 校验 robot.action 的 enum 包含 23 个动作 */
    const CrabToolSpec* action_spec = CrabRegistryFind(&rt->registry, "robot.action");
    if (!action_spec) {
        printf("[FAIL] robot.action not found\n");
        missing++;
    } else {
        const CrabToolParamSpec* p = CrabToolSpecFindParam(action_spec, "action");
        if (!p || p->enumCount != 23) {
            printf("[FAIL] robot.action 'action' enum count=%zu (expected 23)\n",
                   p ? p->enumCount : 0);
            missing++;
        } else {
            printf("[OK] robot.action 'action' enum has %zu values:\n  ", p->enumCount);
            for (size_t j = 0; j < p->enumCount; j++)
                printf("%s%s", j ? "," : "", p->enumValues[j]);
            printf("\n");
        }
        /* 校验 robot.action 其他参数 */
        const char* params[] = {"action", "steps", "speed", "direction", "amount"};
        for (size_t k = 0; k < sizeof(params) / sizeof(params[0]); k++) {
            if (!CrabToolSpecFindParam(action_spec, params[k])) {
                printf("[FAIL] robot.action missing param: %s\n", params[k]);
                missing++;
            }
        }
    }
    /* 校验 robot.pose 的 7 个参数 */
    const CrabToolSpec* pose_spec = CrabRegistryFind(&rt->registry, "robot.pose");
    if (!pose_spec) {
        printf("[FAIL] robot.pose not found\n");
        missing++;
    } else {
        const char* params[] = {
            "left_leg", "right_leg", "left_foot", "right_foot",
            "left_hand", "right_hand", "time_ms",
        };
        for (size_t k = 0; k < sizeof(params) / sizeof(params[0]); k++) {
            if (!CrabToolSpecFindParam(pose_spec, params[k])) {
                printf("[FAIL] robot.pose missing param: %s\n", params[k]);
                missing++;
            }
        }
        printf("[OK] robot.pose has all 7 params\n");
    }

    agent_deinit();
    if (missing) {
        printf("\n[FAIL] %d check(s) failed\n", missing);
        return 1;
    }
    printf("\n[OK] all %zu tools registered, all checks passed\n", cat.toolCount);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: agent_test <text>\n");
        printf("       agent_test --dump-tools   (verify tool registration without LLM)\n");
        printf("  examples:\n");
        printf("    agent_test --dump-tools\n");
        printf("    agent_test hello\n");
        printf("    agent_test \"walk forward 2 steps\"\n");
        printf("    agent_test \"stop\"\n");
        printf("    agent_test \"jump three times\"\n");
        printf("    agent_test \"wave your right hand\"\n");
        printf("    agent_test \"raise both hands\"\n");
        printf("    agent_test \"sit down\"\n");
        printf("    agent_test \"show me a performance\"\n");
        return 1;
    }

    if (strcmp(argv[1], "--dump-tools") == 0)
        return dump_tools_mode();

    AgentConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.llm_base_url = "https://47.102.37.23/v1/chat/completions";
    cfg.llm_api_key = "sk-jcojwurdcgxlgtrgaiuibttmopjzzxxrmnrfoieawqpkqwrj";
    cfg.llm_model = "deepseek-ai/DeepSeek-V4-Flash";
    cfg.llm_timeout_ms = 60000;
    cfg.max_tool_iterations = 5;
    cfg.conversation_history_limit = 10;
    cfg.workspace_root = "/userdata/agent";

    printf("=== Agent Test ===\n");
    printf("[input] %s\n", argv[1]);
    printf("\n");

    if (agent_init(&cfg) != 0) {
        printf("[agent] init failed\n");
        return 1;
    }

    agent_set_callbacks(on_tts, on_tool_call);

    int r = agent_input(argv[1]);

    printf("\n=== Summary ===\n");
    printf("tool_calls: %d\n", g_tool_call_count);
    printf("result: %d\n", r);

    agent_deinit();
    return r;
}
