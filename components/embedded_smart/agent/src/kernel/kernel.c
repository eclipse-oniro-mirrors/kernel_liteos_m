#include "litecrab/kernel.h"

#include "litecrab/hub.h"
#include "litecrab/json.h"
#include "litecrab/observability.h"

#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static int parse_tokens(const char* j, LjToken** out) {
    if (!j)
        return -1;
    const char* p = j;
    while (*p && strchr(" \r\n\t", *p))
        p++;
    LjType root = *p == '[' ? LJ_ARRAY : LJ_OBJECT;
    if (LjValidate(j, root))
        return -1;
    size_t cap = strlen(j) / 2 + 64;
    LjToken* t = calloc(cap, sizeof *t);
    if (!t)
        return -1;
    LjParser parser;
    LjInit(&parser);
    int n = LjParse(&parser, j, strlen(j), t, (unsigned) cap);
    if (n < 0) {
        free(t);
        return -1;
    }
    *out = t;
    return n;
}
int JsonExtractStringField(const char* j, const char* field, char* out, size_t z) {
    if (!j || !field || !out || !z)
        return 0;
    LjToken* t = NULL;
    int n = parse_tokens(j, &t);
    if (n < 1 || t[0].type != LJ_OBJECT) {
        free(t);
        return 0;
    }
    int v = LjObjectGet(j, t, n, 0, field);
    int ok = v >= 0 && t[v].type == LJ_STRING && !LjString(j, &t[v], out, z);
    free(t);
    return ok;
}
int JsonExtractSkillPath(const char* j, char* out, size_t z) {
    return JsonExtractStringField(j, "skillPath", out, z);
}
int TextExtractMarkerInt(const char* t, const char* m, int* out) {
    if (!t || !m || !out)
        return 0;
    const char* p = strstr(t, m);
    if (!p)
        return 0;
    p += strlen(m);
    while (*p == ' ' || *p == '\t')
        p++;
    char* e;
    long v = strtol(p, &e, 10);
    if (e == p)
        return 0;
    *out = (int) v;
    return 1;
}

static const char* type_name(CrabToolValueType t) {
    return t == CRAB_TOOL_VALUE_INT          ? "integer"
           : t == CRAB_TOOL_VALUE_BOOL       ? "boolean"
           : t == CRAB_TOOL_VALUE_STRING_ARRAY ? "array"
                                                : "string";
}
int LlmToolAdapterRenderOpenaiToolsJsonFiltered(const CrabToolCatalogView* c,
                                                int includeSkillTools,
                                                char* out,
                                                size_t z) {
    if (!c || !out || !z)
        return -1;
    LjBuf b;
    LjBufInit(&b, out, z);
    LjAppend(&b, "[");
    size_t emitted = 0;
    for (size_t i = 0; i < c->toolCount; i++) {
        const CrabToolSpec* t = &c->tools[i];
        if (!CrabToolSpecIsDefaultEnabled(t))
            continue;
        if (!includeSkillTools &&
            (!strcmp(t->name, "skill_read") || !strcmp(t->name, "skill_complete")))
            continue;
        if (emitted++)
            LjAppend(&b, ",");
        LjAppend(&b, "{\"type\":\"function\",\"function\":{\"name\":");
        LjAppendJsonString(&b, t->name);
        LjAppend(&b, ",\"description\":");
        LjAppendJsonString(&b, t->description);
        LjAppend(&b, ",\"parameters\":{\"type\":\"object\",\"properties\":{");
        for (size_t j = 0; j < t->paramCount; j++) {
            const CrabToolParamSpec* p = &t->params[j];
            if (j)
                LjAppend(&b, ",");
            LjAppendJsonString(&b, p->name);
            LjAppend(&b, ":{\"type\":\"%s\",\"description\":", type_name(p->type));
            LjAppendJsonString(&b, p->description);
            if (p->type == CRAB_TOOL_VALUE_STRING_ARRAY) {
                LjAppend(&b, ",\"items\":{\"type\":\"string\"}");
                LjAppend(&b, ",\"maxItems\":%d", CRAB_TOOL_ARRAY_MAX_ITEMS);
            }
            if (p->type == CRAB_TOOL_VALUE_STRING && p->enumCount) {
                LjAppend(&b, ",\"enum\":[");
                for (size_t k = 0; k < p->enumCount; k++) {
                    if (k)
                        LjAppend(&b, ",");
                    LjAppendJsonString(&b, p->enumValues[k]);
                }
                LjAppend(&b, "]");
            }
            if (p->type == CRAB_TOOL_VALUE_INT && (p->minInt || p->maxInt))
                LjAppend(&b,
                         ",\"minimum\":%lld,\"maximum\":%lld",
                         (long long) p->minInt,
                         (long long) p->maxInt);
            LjAppend(&b, "}");
        }
        LjAppend(&b, "},\"required\":[");
        int comma = 0;
        for (size_t j = 0; j < t->paramCount; j++)
            if (t->params[j].required) {
                if (comma++)
                    LjAppend(&b, ",");
                LjAppendJsonString(&b, t->params[j].name);
            }
        LjAppend(&b, "],\"additionalProperties\":false}}}");
    }
    LjAppend(&b, "]");
    return b.failed ? -3 : 0;
}
int LlmToolAdapterRenderOpenaiToolsJson(const CrabToolCatalogView* c, char* out, size_t z) {
    return LlmToolAdapterRenderOpenaiToolsJsonFiltered(c, 1, out, z);
}
void LlmToolAdapterFreeCall(CrabToolCall* c) {
    if (!c)
        return;
    for (size_t i = 0; i < c->argCount && i < CRAB_TOOL_MAX_ARGS; i++)
        if (c->args[i].value.type == CRAB_TOOL_VALUE_STRING)
            free((void*) c->args[i].value.stringValue);
        else if (c->args[i].value.type == CRAB_TOOL_VALUE_STRING_ARRAY)
            for (size_t j = 0; j < c->args[i].value.stringArrayValue.count; j++)
                free((void*) c->args[i].value.stringArrayValue.items[j]);
    memset(c, 0, sizeof *c);
}
int LlmToolAdapterParseCall(const CrabToolCatalogView* cat,
                            const char* id,
                            const char* name,
                            const char* args,
                            CrabToolCall* c,
                            char* err,
                            size_t ez) {
    if (!cat || !name || !c)
        return -1;
    memset(c, 0, sizeof *c);
    snprintf(c->callId, sizeof c->callId, "%s", id ? id : "");
    snprintf(c->toolName, sizeof c->toolName, "%s", name);
    const CrabToolSpec* tool = NULL;
    for (size_t i = 0; i < cat->toolCount; i++)
        if (!strcmp(cat->tools[i].name, name)) {
            tool = &cat->tools[i];
            break;
        }
    if (!tool) {
        snprintf(err, ez, "tool not found");
        return -1;
    }
    const char* j = args ? args : "{}";
    LjToken* t;
    int n = parse_tokens(j, &t);
    if (n < 1 || t[0].type != LJ_OBJECT) {
        snprintf(err, ez, "invalid tool arguments JSON");
        free(t);
        return -1;
    }
    int i = 1;
    while (i < n && t[i].start < t[0].end) {
        if (c->argCount >= CRAB_TOOL_MAX_ARGS) {
            snprintf(err, ez, "too many tool arguments");
            goto fail;
        }
        char key[CRAB_TOOL_ARG_NAME_MAX];
        if (t[i].type != LJ_STRING || LjString(j, &t[i], key, sizeof key)) {
            snprintf(err, ez, "invalid argument name");
            goto fail;
        }
        int v = i + 1;
        const CrabToolParamSpec* p = CrabToolSpecFindParam(tool, key);
        if (!p) {
            snprintf(err, ez, "unknown tool argument: %s", key);
            goto fail;
        }
        CrabToolArg* a = &c->args[c->argCount];
        snprintf(a->name, sizeof a->name, "%s", key);
        a->value.type = p->type;
        if (p->type == CRAB_TOOL_VALUE_STRING) {
            if (t[v].type != LJ_STRING) {
                snprintf(err, ez, "string argument expected");
                goto fail;
            }
            size_t z = (size_t) (t[v].end - t[v].start) + 1;
            char* s = malloc(z);
            if (!s || LjString(j, &t[v], s, z)) {
                free(s);
                snprintf(err, ez, "out of memory");
                goto fail;
            }
            a->value.stringValue = s;
        } else if (p->type == CRAB_TOOL_VALUE_INT) {
            if (LjInt64(j, &t[v], &a->value.intValue)) {
                snprintf(err, ez, "integer argument expected");
                goto fail;
            }
        } else if (p->type == CRAB_TOOL_VALUE_BOOL) {
            if (LjBool(j, &t[v], &a->value.boolValue)) {
                snprintf(err, ez, "boolean argument expected");
                goto fail;
            }
        } else if (p->type == CRAB_TOOL_VALUE_STRING_ARRAY) {
            if (t[v].type != LJ_ARRAY || t[v].size > CRAB_TOOL_ARRAY_MAX_ITEMS) {
                snprintf(err, ez, "string array argument expected");
                goto fail;
            }
            for (int arrayIndex = 0; arrayIndex < t[v].size; arrayIndex++) {
                int item = LjArrayGet(t, n, v, arrayIndex);
                if (item < 0 || t[item].type != LJ_STRING) {
                    snprintf(err, ez, "string array item expected");
                    for (size_t k = 0; k < a->value.stringArrayValue.count; k++)
                        free((void*) a->value.stringArrayValue.items[k]);
                    a->value.stringArrayValue.count = 0;
                    goto fail;
                }
                size_t itemSize = (size_t) (t[item].end - t[item].start) + 1;
                char* text = malloc(itemSize);
                if (!text || LjString(j, &t[item], text, itemSize)) {
                    free(text);
                    snprintf(err, ez, "out of memory");
                    for (size_t k = 0; k < a->value.stringArrayValue.count; k++)
                        free((void*) a->value.stringArrayValue.items[k]);
                    a->value.stringArrayValue.count = 0;
                    goto fail;
                }
                a->value.stringArrayValue.items[a->value.stringArrayValue.count++] = text;
            }
        } else {
            snprintf(err, ez, "unsupported argument type");
            goto fail;
        }
        c->argCount++;
        i = LjSkip(t, n, v);
    }
    free(t);
    return 0;
fail:
    free(t);
    LlmToolAdapterFreeCall(c);
    return -1;
}

static int array_append(char* json, size_t z, const char* obj) {
    if (!json || !obj || LjValidate(json, LJ_ARRAY) || LjValidate(obj, LJ_OBJECT))
        return -1;
    size_t n = strlen(json), o = strlen(obj);
    while (n && strchr(" \r\n\t", json[n - 1]))
        n--;
    if (!n || json[n - 1] != ']')
        return -1;
    size_t p = n - 1;
    int empty = 1;
    for (size_t i = 0; i < p; i++)
        if (json[i] == '[') {
        } else if (!strchr(" \r\n\t", json[i])) {
            empty = 0;
            break;
        }
    if (p + o + 3 > z)
        return -1;
    json[p] = 0;
    snprintf(json + p, z - p, "%s%s]", empty ? "" : ",", obj);
    return 0;
}
static int append_role(char* j, size_t z, const char* role, const char* content) {
    char* obj = malloc(strlen(content ? content : "") * 6 + 128);
    if (!obj)
        return -1;
    LjBuf b;
    LjBufInit(&b, obj, strlen(content ? content : "") * 6 + 128);
    LjAppend(&b, "{\"role\":");
    LjAppendJsonString(&b, role);
    LjAppend(&b, ",\"content\":");
    LjAppendJsonString(&b, content ? content : "");
    LjAppend(&b, "}");
    int rc = b.failed ? -1 : array_append(j, z, obj);
    free(obj);
    return rc;
}
int MessageTreeAppendAssistant(char* j, size_t z, const char* c) {
    return append_role(j, z, "assistant", c);
}
int MessageTreeAppendUser(char* j, size_t z, const char* c) {
    WorkingMemoryBeginRequest(c);
    return append_role(j, z, "user", c);
}

int ContextBuildSystemPromptEx(char* b, size_t z, int skills) {
    static const char base[] =
        "# Robot Agent\n\n"
        "You are a robot control assistant. You can control robot movement, "
        "speech, and actions through tool calls. Respond concisely in the user's "
        "language.\n\n"
        "Tool-use rules:\n- The tools and argument schemas supplied by the current API request "
        "are authoritative; call only tools present there.\n- "
        "Use tools when the task requires robot movement, speech, or other actions.\n- "
        "Do not claim work is done before executing the "
        "required tool calls and "
        "inspecting results.\n- Treat tool outputs as intermediate state unless "
        "they fully answer "
        "the user request.\n- Final answers should be brief and should not "
        "repeat large tool outputs.\n- "
        /* 三条为 2026-10-16 板测故事重讲缺陷所加(本地 diff, 待回传):
         * 模型把两个动作 JSON 拼进一次 robot.action 调用(call stuffing),
         * 工具无硬件失败后 round 2 "好的我来讲个故事吧"整个重开。 */
        "Pass exactly one JSON object per tool call; never concatenate multiple "
        "JSON objects into one arguments string.\n- "
        "When a tool call fails, note the limitation briefly and continue the "
        "current task in speech; never restart or repeat content you have "
        "already delivered.\n- "
        "After a robot tool fails, do not call robot tools again in the same "
        "reply; finish the reply in speech alone. Deliver storytelling and "
        "other narration in the reply text.\n- "
        /* 两条为 2026-10-19 板测"讲故事"所加(本地 diff, 待回传):
         * 1) 跨工具轮重复开场白("好的我来讲个故事"念两遍, greeting+hand_wave 连挥两次);
         * 2) 故事结尾文案念"挥挥手~"但 calls=0 没调工具, 只念不动。 */
        "When your narration says the robot performs an action (waving, "
        "dancing, walking, bowing...), call the matching robot tool in that "
        "same reply; never describe an action in speech without calling the "
        "tool.\n- "
        "After a tool call succeeds, continue directly with new content only; "
        "never repeat agreement, plans, or opening lines already spoken, and "
        "avoid calling another similar action for the same intent.\n";
    LjBuf out;
    LjBufInit(&out, b, z);
    LjAppend(&out, "%s", base);
    if (skills) {
        SkillEntry e[32];
        int n = SkillGetEntries(e, 32);
        if (n > 0) {
            LjAppend(&out,
                     "\nAvailable runtime skills (selection metadata only):\n"
                     "The Skill Router, not the assistant, decides which Skill may run. Each "
                     "description explains its function; the example shows how to load it after "
                     "the router selects it.\n");
            for (int i = 0; i < n; i++)
                LjAppend(&out,
                         "\n## skill_%d\nname: %s\nfunction: %s\nusage example: "
                         "`skill_read({\"skillPath\":\"%s\",\"fileName\":\"SKILL.md\"})`\n",
                         i,
                         e[i].name,
                         e[i].prompt,
                         e[i].name);
        } else {
            LjAppend(&out, "\nAvailable runtime skills:\n- None loaded.\n");
        }
    }
    LjAppend(&out,
             "\nSkill rules:\n- Always call `skill_read` before executing any skill workflow, "
             "even if you recall it from conversation history.\n- Use the skill list only for "
             "understanding capabilities; obey the Skill Router decision and never self-select "
             "or switch Skills.\n- When a Skill is selected, call `skill_read` with the selected "
             "Skill name as `skillPath`.\n- Load every skill instruction or reference file with "
             "`skill_read`, never with other tools.\n- Read each file under `references/` with "
             "`skill_read` "
             "at most once per workflow. After a successful read, reuse the loaded content and "
             "never issue duplicate or otherwise unnecessary reads for the same reference file.\n- "
             "After satisfying the loaded Skill completion checks, "
             "call `skill_complete` with a short evidence summary before the final answer.\n");
    if (out.failed)
        return -1;
    return WorkingMemoryAppendPrompt(b, z);
}

static CrabRuntime runtime;
static char toolsJson[65536];
static char baseToolsJson[65536];
static pthread_t thread;
static atomic_int running;
static AgentToolCallCallback g_tool_call_cb = NULL;
void AgentLoopSetToolCallCallback(AgentToolCallCallback cb) { g_tool_call_cb = cb; }
CrabRuntime* AgentLoopRuntime(void) {
    return &runtime;
}
int AgentLoopRefreshToolCatalog(void) {
    CrabToolCatalogView cat;
    if (CrabRuntimeGetToolCatalog(&runtime, &cat))
        return -1;
    return LlmToolAdapterRenderOpenaiToolsJson(&cat, toolsJson, sizeof toolsJson) ||
           LlmToolAdapterRenderOpenaiToolsJsonFiltered(&cat, 0, baseToolsJson, sizeof baseToolsJson) ||
           SkillRouterInit(&cat)
               ? -1
               : 0;
}
int AgentLoopInit(const CrabRuntimeConfig* rc, const LlmConfig* lc, const MemoryConfig* mc) {
    MessageBusInit();
    if (!rc || !rc->workspaceRoot || AgentSessionStoreConfigure(rc->workspaceRoot))
        return -1;
    AgentSessionStateInit();
    WorkingMemoryInit();
    SkillSupersetReset();
    char skillRoot[CRAB_RUNTIME_PATH_MAX];
    int skillPathLength =
        rc && rc->workspaceRoot
            ? snprintf(skillRoot, sizeof skillRoot, "%s/skills", rc->workspaceRoot)
            : -1;
    if (skillPathLength < 0 || (size_t) skillPathLength >= sizeof skillRoot)
        return -1;
    SkillLoadAllFrom(skillRoot);
    if (CrabRuntimeInit(&runtime, rc) || CrabRuntimeRegisterBuiltinTools(&runtime))
        return -1;
    if (mc) {
        if (MemoryInit(mc, rc->workspaceRoot)) {
            LogPrint("[memory] initialization failed; refusing to start without memory path protection");
            return -1;
        }
        else if (mc->enabled && CrabRuntimeRegisterMemoryTool(&runtime)) {
            MemoryShutdown();
            LogPrint("[memory] tool registration failed; refusing to start with incomplete memory support");
            return -1;
        }
    }
    /* 注册机器人工具 (robot.move/speak/stop/action/pose/play_sound)
     * 定义在 src/runtime/robot_tools.c, 通过 tool_call_cb 下发到舵机模块 */
    if (CrabRuntimeRegisterRobotTools(&runtime)) {
        LogPrint("[robot] tool registration failed; refusing to start without robot tools");
        return -1;
    }
    CrabRuntimeStart(&runtime);
    if (AgentLoopRefreshToolCatalog())
        return -1;
    if (LlmInit(lc))
        return -1;
    return 0;
}
static int append_tool_exchange(char* messages,
                                size_t z,
                                const LlmResponse* r,
                                char outputs[][AGENT_TOOL_OUTPUT_MAX]) {
    /* content 预算: 工具轮的旁白文字也要写回历史(*6 为 JSON 转义上界, 同 append_role) */
    size_t cap = 4096 + (r->text ? strlen(r->text) * 6 : 0);
    for (int i = 0; i < r->callCount; i++)
        cap += strlen(r->calls[i].input ? r->calls[i].input : "{}") + strlen(outputs[i]) + 512;
    char* obj = malloc(cap);
    if (!obj)
        return -1;
    LjBuf b;
    LjBufInit(&b, obj, cap);
    /* 2026-10-19 修复: 此前工具轮 assistant 消息 content 恒写 null, 模型看不见
     * 自己当轮已念出的旁白("我来讲个XX故事"), 下一轮整个重开甚至换主题
     * (板测: 机器人探险->毛毛虫)。OpenAI 协议允许 content 与 tool_calls 并存,
     * 把 r->text 带回; 无旁白的纯工具轮仍写 null 保持原行为。 */
    LjAppend(&b, "{\"role\":\"assistant\",\"content\":");
    if (r->text && *r->text)
        LjAppendJsonString(&b, r->text);
    else
        LjAppend(&b, "null");
    LjAppend(&b, ",\"tool_calls\":[");
    for (int i = 0; i < r->callCount; i++) {
        if (i)
            LjAppend(&b, ",");
        LjAppend(&b, "{\"id\":");
        LjAppendJsonString(&b, r->calls[i].id);
        LjAppend(&b, ",\"type\":\"function\",\"function\":{\"name\":");
        LjAppendJsonString(&b, r->calls[i].name);
        LjAppend(&b, ",\"arguments\":");
        LjAppendJsonString(&b, r->calls[i].input ? r->calls[i].input : "{}");
        LjAppend(&b, "}}");
    }
    LjAppend(&b, "]}");
    int rc = b.failed ? -1 : array_append(messages, z, obj);
    free(obj);
    if (rc)
        return rc;
    for (int i = 0; i < r->callCount; i++) {
        cap = strlen(outputs[i]) * 6 + 256;
        obj = malloc(cap);
        if (!obj)
            return -1;
        LjBufInit(&b, obj, cap);
        LjAppend(&b, "{\"role\":\"tool\",\"tool_call_id\":");
        LjAppendJsonString(&b, r->calls[i].id);
        LjAppend(&b, ",\"content\":");
        LjAppendJsonString(&b, outputs[i]);
        LjAppend(&b, "}");
        rc = b.failed ? -1 : array_append(messages, z, obj);
        free(obj);
        if (rc)
            return rc;
    }
    return 0;
}
static int append_route_prompt(char* system, size_t z, const SkillRouterResult* route) {
    size_t used = strlen(system);
    int n;
    if (!route || route->action == SKILL_ROUTE_BASE) {
        n = snprintf(system + used,
                     z - used,
                     "\nSkill Router decision: no specialized skill is required. Handle this "
                     "request as the base agent. Do not start, infer, or load a skill workflow "
                     "in this turn.\n");
        if (n < 0 || (size_t) n >= z - used)
            return -1;
        used += (size_t) n;
    } else {
        n = snprintf(system + used,
                     z - used,
                     "\nSkill Router decision: select `%s`. Call `skill_read` for exactly this "
                     "Skill before following its workflow. Do not select or load another Skill.\n",
                     route->skillName);
    }
    return n < 0 || (size_t) n >= z - used ? -1 : 0;
}
char* run_round(const char* input,
                       const char* sessionId,
                       const char* requestId,
                       const SkillRouterResult* route,
                       AgentTraceScope* scope,
                       const void* request_ptr) {
    const LiteMsg* request = (const LiteMsg*)request_ptr;
    size_t cap = AGENT_MAX_CONVERSATION_BYTES + 32768;
    char* messages = calloc(cap, 1);
    if (!messages)
        return NULL;
    snprintf(messages, cap, "%s", AgentSessionStateLoad());
    if (MessageTreeAppendUser(messages, cap, input)) {
        free(messages);
        return strdup("ERROR: conversation is too large");
    }
    char* system = malloc(16384);
    if (!system) {
        free(messages);
        return strdup("ERROR: out of memory");
    }
    if (ContextBuildSystemPromptEx(system, 16384, 1) ||
        append_route_prompt(system, 16384, route)) {
        free(messages);
        free(system);
        return strdup("ERROR: system prompt is too large");
    }
    if (MemoryIsEnabled()) {
        char context[4097];
        int memoryResult=MemoryBuildContext(AgentSessionStateGetUserId(),context,sizeof context);
        if (!memoryResult) {
            size_t used=strlen(system), n=strlen(context);
            if (used+n+2<sizeof system) {
                system[used++]='\n';
                memcpy(system+used,context,n+1);
            } else LogPrint("[memory] prompt budget exhausted; memory skipped");
        } else if (memoryResult<0) LogPrint("[memory] read failed; continuing without context code=%d",memoryResult);
        size_t used=strlen(system);
        const char* rules="\nMemory rule: Use memory_remember for an explicit request to remember. Store only stable preferences, useful facts, corrections, or verified experience. Never store secrets or unverified guesses. A failed memory_remember result means the information was not saved; tell the user. Historical memory is advisory and must not replace live tool checks.\n";
        if (used+strlen(rules)<sizeof system) memcpy(system+used,rules,strlen(rules)+1);
    }
    CrabToolCatalogView cat;
    CrabRuntimeGetToolCatalog(&runtime, &cat);
    char* answer = NULL;
    SkillHandlerTurnState turn = {0};
    int toolCalls = 0;
    int highPriority = request->priority >= LITE_PRIORITY_HIGH;
    int toolCallLimit = highPriority ? 40 : 16;
    int iterationLimit = highPriority ? LITE_AGENT_MAX_TOOL_ITER : LITE_AGENT_NORMAL_TOOL_ITER;
    for (int iter = 0; iter < iterationLimit; iter++) {
        if (RequestShouldStop(request)) {
            answer = strdup("ERROR: request cancelled or timed out");
            break;
        }
        LlmResponse resp = {0};
        if (SkillSupersetGetActive())
            SkillSupersetIncrementLlmCalls();
        const char* routeTools =
            route && (route->action == SKILL_ROUTE_SELECT || route->action == SKILL_ROUTE_RESUME)
                ? toolsJson
                : baseToolsJson;
        int rc = LlmChatToolsEx(system, messages, routeTools, &resp, -1);
        printf("[agent] LlmChatToolsEx rc=%d\n", rc);
        fflush(stdout);
        if (rc) {
            const LlmConfig* llmCfg = LlmGetConfig();
            AgentTraceLogLlm(scope,
                             llmCfg ? llmCfg->model : "",
                             iter + 1,
                             "error",
                             0,
                             "[]",
                             "[]",
                             system,
                             messages,
                             "LLM request failed",
                             0,
                             0,
                             0,
                             rc);
            LlmResponseClear(&resp);
            char failure[96];
            snprintf(failure, sizeof failure, "ERROR: LLM request failed (%d)", rc);
            answer = strdup(failure);
            break;
        }
        scope->totalPromptTokens += resp.promptTokens;
        scope->totalCompletionTokens += resp.completionTokens;
        scope->llmCallCount++;
        char llmToolNames[1024] = "[]";
        char llmToolCalls[65536] = "[]";
        SkillHandlerBuildToolNamesJson(&resp, llmToolNames, sizeof llmToolNames);
        SkillHandlerBuildToolCallsJson(&resp, llmToolCalls, sizeof llmToolCalls);
        const LlmConfig* llmCfg = LlmGetConfig();
        AgentTraceLogLlm(scope,
                         llmCfg ? llmCfg->model : "",
                         iter + 1,
                         "ok",
                         resp.toolUse,
                         llmToolNames,
                         llmToolCalls,
                         system,
                         messages,
                         resp.text,
                         resp.promptTokens,
                         resp.completionTokens,
                         resp.reasoningTokens,
                         0);
        if (!resp.toolUse) {
            answer = SkillHandlerExtractFinalText(resp.text, resp.textLen);
            MessageTreeAppendAssistant(messages, cap, resp.text);
            const SkillStateSuperset* activeSkill = SkillSupersetGetActive();
            if (activeSkill) {
                SkillSuspendQueuePush(activeSkill);
                SkillHandlerSuspend(scope, sessionId, requestId);
            }
            LlmResponseClear(&resp);
            break;
        }
        SkillHandlerOnSkillStart(scope, &resp, sessionId, requestId);
        char outputs[LLM_MAX_TOOL_CALLS][AGENT_TOOL_OUTPUT_MAX];
        memset(outputs, 0, sizeof outputs);
        for (int i = 0; i < resp.callCount; i++) {
            if (RequestShouldStop(request)) {
                /* 2026-10-19: 跳过只写回 LLM 不上控制台, 板测工具没执行时
                 * 日志一片空白无从定位(死线砍工具那次), 补打印。 */
                printf("[agent] tool[%d] '%s' skipped: request deadline reached\n",
                       i, resp.calls[i].name ? resp.calls[i].name : "?");
                fflush(stdout);
                snprintf(outputs[i], sizeof outputs[i], "tool cancelled: request deadline reached");
                continue;
            }
            if (toolCalls >= toolCallLimit) {
                printf("[agent] tool[%d] '%s' skipped: tool limit %d reached\n",
                       i, resp.calls[i].name ? resp.calls[i].name : "?", toolCallLimit);
                fflush(stdout);
                snprintf(outputs[i], sizeof outputs[i], "tool limit reached: maximum %d calls", toolCallLimit);
                continue;
            }
            toolCalls++;
            if (g_tool_call_cb) {
                /* 通过回调输出 tool_call 给舵机模块, 舵机自己解析执行 */
                int r = g_tool_call_cb(resp.calls[i].name,
                                        resp.calls[i].input ? resp.calls[i].input : "{}",
                                        outputs[i], sizeof outputs[i]);
                if (r != 0)
                    snprintf(outputs[i], sizeof outputs[i], "tool execution failed: %d", r);
                AgentTraceLogTool(scope,
                                  NULL,
                                  resp.calls[i].name,
                                  resp.calls[i].input,
                                  outputs[i],
                                  r,
                                  r == 0 ? "ok" : "error");
            } else {
                CrabToolCall call;
                CrabToolResponse tr;
                char err[256];
                if (LlmToolAdapterParseCall(&cat,
                                            resp.calls[i].id,
                                            resp.calls[i].name,
                                            resp.calls[i].input,
                                            &call,
                                            err,
                                            sizeof err))
                    snprintf(outputs[i], sizeof outputs[i], "tool parse error: %s", err);
                else {
                    CrabRuntimeCallTool(&runtime, &call, &tr);
                    size_t copy = strnlen(tr.content, sizeof tr.content);
                    if (copy >= sizeof outputs[i])
                        copy = sizeof outputs[i] - 1;
                    memcpy(outputs[i], tr.content, copy);
                    outputs[i][copy] = 0;
                    AgentTraceLogTool(scope,
                                      NULL,
                                      call.toolName,
                                      resp.calls[i].input,
                                      outputs[i],
                                      tr.toolExitCode,
                                      tr.success ? "ok" : "error");
                    SkillHandlerOnToolResult(scope, &call, outputs[i], sessionId, requestId, &turn);
                    LlmToolAdapterFreeCall(&call);
                }
            }
        }
        if (append_tool_exchange(messages, cap, &resp, outputs)) {
            answer = strdup("ERROR: conversation is too large");
            LlmResponseClear(&resp);
            break;
        }
        LlmResponseClear(&resp);
    }
    if (!answer)
        answer = strdup("Reached maximum tool iterations before the agent could finish.");
    if (SkillSupersetGetActive()) {
        SkillSuspendQueuePush(SkillSupersetGetActive());
        SkillHandlerSuspend(scope, sessionId, requestId);
    }
    AgentSessionStateSave(messages);
    free(messages);
    free(system);
    return answer;
}

static void finish_request(const LiteMsg* request, char* content) {
    if (request->replyMode == LITE_REPLY_ACK_ONLY) {
        free(content);
        return;
    }
    LiteMsg response = {0};
    response.type = LITE_MSG_CHAT;
    response.replyMode = request->replyMode;
    snprintf(response.requestId, sizeof response.requestId, "%s", request->requestId);
    snprintf(response.sessionId, sizeof response.sessionId, "%s", request->sessionId);
    response.content = content ? content : strdup("ERROR: internal allocation failure");
    if (!response.content) {
        RequestCancel(request->requestId);
        return;
    }
    if (!strncmp(response.content,
                 "ERROR: LLM request failed",
                 strlen("ERROR: LLM request failed")) ||
        !strncmp(response.content,
                 "ERROR: request cancelled or timed out",
                 strlen("ERROR: request cancelled or timed out")) ||
        !strncmp(response.content,
                 "ERROR: session unavailable",
                 strlen("ERROR: session unavailable")) ||
        !strncmp(response.content,
                 "ERROR: session capacity exhausted",
                 strlen("ERROR: session capacity exhausted")) ||
        !strncmp(response.content,
                 "ERROR: internal allocation failure",
                 strlen("ERROR: internal allocation failure")))
        response.completionStatus = LITE_COMPLETION_RETRYABLE;
    else if (!strncmp(response.content, "ERROR:", 6))
        response.completionStatus = LITE_COMPLETION_PERMANENT;
    else
        response.completionStatus = LITE_COMPLETION_HANDLED;
    if (RequestComplete(&response))
        free(response.content);
}

static void* agent_main(void* x) {
    (void) x;
    printf("[agent] thread started\n");
    fflush(stdout);
    while (atomic_load(&running)) {
        LiteMsg m = {0};
        MessageBusPopInbound(&m);
        if (!atomic_load(&running)) {
            LiteMsgClear(&m);
            break;
        }
        printf("[agent] thread got msg type=%d\n", (int)m.type);
        fflush(stdout);
        if (m.type == LITE_MSG_SESSION_OPEN) {
            /* A transport connection is not a durable Session. Create the
             * Session lazily when its first chat message arrives. */
        } else if (m.type == LITE_MSG_SESSION_CLOSE)
            AgentSessionStateReleaseById(m.sessionId);
        else if (m.type == LITE_MSG_CHAT) {
            printf("[agent] CHAT msg, content=%s\n", m.content ? m.content : "(null)");
            fflush(stdout);
            int shouldStop = RequestShouldStop(&m);
            printf("[agent] RequestShouldStop=%d\n", shouldStop); fflush(stdout);
            if (shouldStop) {
                finish_request(&m, strdup("ERROR: request cancelled or timed out"));
                LiteMsgClear(&m);
                continue;
            }
            printf("[agent] before SessionAcquire\n"); fflush(stdout);
            SessionHandle session = {0};
            if (AgentSessionStateAcquire(m.sessionId, 1, &session) ||
                AgentSessionStateUse(&session)) {
                printf("[agent] SessionAcquire failed\n"); fflush(stdout);
                finish_request(&m, strdup("ERROR: session capacity exhausted"));
                LiteMsgClear(&m);
                continue;
            }
            if (AgentSessionStateBindUser(m.userId)) {
                finish_request(&m, strdup("ERROR: session is owned by another user"));
                AgentSessionStateRelease(&session);
                LiteMsgClear(&m);
                continue;
            }
            AgentTraceScope trace;
            AgentTraceStart(&trace, m.userId, m.sessionId, m.content);
            LlmRequestScopeBegin(m.deadlineMs, m.priority >= LITE_PRIORITY_HIGH ? 40 : 8);
            printf("[agent] before SkillRouterRun\n"); fflush(stdout);
            SkillRouterResult route = SkillRouterRun(AgentSessionStateLoad(),
                                                     m.content,
                                                     m.replyToRunId,
                                                     m.replyToInterruptId,
                                                     m.correlationToken,
                                                     &trace);
            printf("[agent] after SkillRouterRun action=%d\n", (int)route.action); fflush(stdout);
            char routeEvent[1024];
            snprintf(routeEvent,
                     sizeof routeEvent,
                     "event=skill.candidates.resolved session_id=%s action=%d skill=%s "
                     "confidence=%.3f reason=%s source=%s evidence=%s",
                     m.sessionId,
                     route.action,
                     route.skillName,
                     route.confidence,
                     route.reasonCode,
                     route.source,
                     route.evidence);
            AgentTraceLogMessage("INFO", "skill_router", routeEvent);
            AgentSessionStateSetSkillRoute(route.skillName,
                                           route.action == SKILL_ROUTE_SELECT ||
                                               route.action == SKILL_ROUTE_RESUME);
            if (route.action == SKILL_ROUTE_RESUME)
                SkillRouterRecover(
                    route.skillName, route.skillRunId, &trace, m.sessionId, m.requestId);
            char* ans =
                route.action == SKILL_ROUTE_ERROR
                    ? strdup(route.question)
                    : run_round(
                          m.content ? m.content : "", m.sessionId, m.requestId, &route, &trace, &m);
            AgentTraceFinish(&trace,
                             route.action != SKILL_ROUTE_ERROR && ans && strncmp(ans, "ERROR:", 6)
                                 ? "ok"
                                 : "error",
                             ans);
            LlmRequestScopeEnd();
            finish_request(&m, ans);
            AgentSessionStateRelease(&session);
        }
        LiteMsgClear(&m);
    }
    return NULL;
}
int AgentLoopStart(void) {
    if (atomic_load(&running))
        return 0;
    atomic_store(&running, 1);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);  /* 512KB 栈, run_round+agent_main 栈帧大 */
    if (pthread_create(&thread, &attr, agent_main, NULL)) {
        pthread_attr_destroy(&attr);
        atomic_store(&running, 0);
        return -1;
    }
    pthread_attr_destroy(&attr);
    return 0;
}
void AgentLoopStop(void) {
    MemoryShutdown();
    if (!atomic_load(&running))
        return;
    LlmCancelActiveRequests();
    atomic_store(&running, 0);
    LiteMsg m = {.type = LITE_MSG_EXCEPTION};
    m.content = strdup("stop");
    MessageBusPushInbound(&m);
    pthread_join(thread, NULL);
    CrabRuntimeStop(&runtime);
}
