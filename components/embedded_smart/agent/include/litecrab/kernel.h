#ifndef LITECRAB_KERNEL_H
#define LITECRAB_KERNEL_H
#include "litecrab/observability.h"
#include "litecrab/runtime.h"
#include "litecrab/session.h"
#include "litecrab/memory.h"
#include "litecrab/working_memory.h"

#include <stddef.h>
#define AGENT_TOOL_OUTPUT_MAX (16 * 1024)
#define LLM_MAX_TOOL_CALLS 8
#define LLM_RESPONSE_TEXT_MAX (64 * 1024)

typedef struct {
    char id[64], name[64];
    char* input;
    size_t inputLen, inputCapacity;
} LlmToolCall;
typedef struct {
    char text[LLM_RESPONSE_TEXT_MAX];
    size_t textLen;
    LlmToolCall calls[LLM_MAX_TOOL_CALLS];
    int callCount, toolUse, promptTokens, completionTokens, totalTokens, reasoningTokens;
} LlmResponse;
typedef struct {
    const char* baseUrl;
    char host[256];
    char vhost[256];  /* SNI/Host 头用 (如 api.siliconflow.cn), host 用于连接(IP) */
    int port;
    char path[512];
    int useTls;
    const char *apiKey, *model;
    int maxTokens;
    double temperature;
    int stream;
    const char* reasoningEffort;
    int timeoutMs;
} LlmConfig;
int LlmInit(const LlmConfig*);
void LlmRequestScopeBegin(long long deadlineMs, int maxCalls);
void LlmRequestScopeEnd(void);
void LlmCancelActiveRequests(void);
const LlmConfig* LlmGetConfig(void);
int LlmBuildChatToolsRequestJson(const char*, const char*, int, char*, size_t);
int LlmChatToolsEx(const char* systemPrompt,
                   const char* messagesJson,
                   const char* toolsJson,
                   LlmResponse*,
                   int stream);

/* 流式正文增量钩子(SPEC_AGENT_ADAPTATION 修改点①参考实现, 2026-10-15):
 * 注册后 LlmChatToolsEx 改走 SSE 流式(llm_client 的 llm_chat_stream_raw),
 * 每个正文 delta(UTF-8, 已反转义, reasoning 不透传)同步回调一次;
 * 每轮流正文结束(含中止/出错)补调一次 delta=NULL,len=0 — 调用方据此收尾。
 * 返回非 0 = 请求中止生成(播放期打断场景): 传输停读, LlmChatToolsEx 返回
 * 错误让本轮 run_round 停止。hook 为 NULL(默认)时维持原非流式行为。
 * 单线程约定: 钩子在 LlmChatToolsEx 调用线程内同步执行(阻塞回调=背压)。 */
typedef int (*LlmTextDeltaFn)(void* ud, const char* delta, int len);
void LlmSetTextDeltaHook(LlmTextDeltaFn fn, void* ud);
int LlmParseResponseBody(const char*, int stream, LlmResponse*);
void LlmResponseClear(LlmResponse*);
int LlmToolAdapterRenderOpenaiToolsJson(const CrabToolCatalogView*, char*, size_t);
int LlmToolAdapterRenderOpenaiToolsJsonFiltered(const CrabToolCatalogView*,
                                                int includeSkillTools,
                                                char*,
                                                size_t);
int LlmToolAdapterParseCall(const CrabToolCatalogView*,
                            const char*,
                            const char*,
                            const char*,
                            CrabToolCall*,
                            char*,
                            size_t);
void LlmToolAdapterFreeCall(CrabToolCall*);
int JsonExtractStringField(const char*, const char*, char*, size_t);
int JsonExtractSkillPath(const char*, char*, size_t);
int TextExtractMarkerInt(const char*, const char*, int*);
int MessageTreeAppendAssistant(char*, size_t, const char*);
int MessageTreeAppendUser(char*, size_t, const char*);

int ContextBuildSystemPromptEx(char*, size_t, int);

typedef enum {
    SKILL_EXEC_PHASE_INACTIVE = 0,
    SKILL_EXEC_PHASE_RUNNING,
    SKILL_EXEC_PHASE_WAITING_INPUT,
    SKILL_EXEC_PHASE_RESUMING,
    SKILL_EXEC_PHASE_SUCCESS,
    SKILL_EXEC_PHASE_FAILED,
    SKILL_EXEC_PHASE_PARTIAL
} SkillExecPhase;
typedef struct {
    char skillName[128], skillRunId[64], scriptsDir[256];
} SkillBasicInfo;
typedef struct {
    int active;
    SkillExecPhase phase;
    int stepCount, llmCalls;
    long long createdTimeMs, lastActiveTimeMs;
    char lastOutcome[16];
} SkillExecState;
typedef struct {
    char skillSpanId[64];
    int totalPromptTokens, totalCompletionTokens;
} SkillTraceInfo;
typedef struct {
    SkillBasicInfo basic;
    SkillExecState exec;
    SkillTraceInfo trace;
} SkillStateSuperset;
typedef struct {
    SkillBasicInfo basic;
    SkillExecState exec;
    char sessionId[64];
    char interruptionId[64], correlationToken[64];
} SkillSuspendEntry;
typedef struct {
    char name[128], prompt[2048], path[512], packageRoot[1024], scriptsDir[1024];
    char definitionHash[65];
} SkillEntry;
const SkillStateSuperset* SkillSupersetGetActive(void);
int SkillSupersetActivate(const char*, const char*);
int SkillSupersetFinish(SkillExecPhase, const char*);
void SkillSupersetReset(void);
void SkillSupersetOnSessionClose(void);
int SkillSupersetUpdateExecPhase(SkillExecPhase, const char*);
int SkillSupersetIncrementStep(int*);
int SkillSupersetIncrementLlmCalls(void);
const char* SkillExecPhaseToString(SkillExecPhase);
int SkillSuspendQueuePush(const SkillStateSuperset*);
int SkillSuspendQueuePopByName(const char*, SkillSuspendEntry*);
void SkillSuspendQueueClear(void);
void SkillSuspendQueueClearSession(const char*);
int SkillSuspendQueueGetCount(void);
int SkillSuspendQueueBuildSimpleContext(char*, size_t);
int SkillSupersetReactivate(const SkillSuspendEntry*);
int SkillLoadAll(void);
int SkillLoadAllFrom(const char*);
int SkillGetEntries(SkillEntry*, int);
const char* SkillGetCanonicalRoot(void);
int SkillReadContent(const char*, const char*, char**, size_t*, int*);

typedef enum {
    SKILL_RUN_CREATED = 0,
    SKILL_RUN_READY,
    SKILL_RUN_RUNNING,
    SKILL_RUN_WAITING_INPUT,
    SKILL_RUN_SUSPENDED,
    SKILL_RUN_COMPLETED,
    SKILL_RUN_FAILED,
    SKILL_RUN_CANCELLED
} SkillRunStatus;
typedef struct {
    char skillRunId[64], sessionId[64], skillName[128], definitionHash[65];
    char interruptionId[64], correlationToken[64];
    SkillRunStatus status;
    unsigned long revision;
    int stepCount, llmCalls;
    long long createdTimeMs, updatedTimeMs;
    char reason[64];
} SkillRunSnapshot;
int SkillRunGetSnapshot(const char*, const char*, SkillRunSnapshot*);
int SkillRunListPending(const char*, SkillRunSnapshot*, int);
const char* SkillRunStatusToString(SkillRunStatus);
typedef struct {
    int relatedToolSeen, hasLastExitCode, lastExitCode;
} SkillHandlerTurnState;
void SkillHandlerOnSkillStart(AgentTraceScope*, const LlmResponse*, const char*, const char*);
void SkillHandlerOnToolResult(AgentTraceScope*,
                              const CrabToolCall*,
                              const char*,
                              const char*,
                              const char*,
                              SkillHandlerTurnState*);
void SkillHandlerSuspend(AgentTraceScope*, const char*, const char*);
void SkillHandlerFinish(AgentTraceScope*, const char*, const char*, SkillExecPhase);
char* SkillHandlerExtractFinalText(const char*, size_t);
int SkillHandlerBuildToolNamesJson(const LlmResponse*, char*, size_t);
int SkillHandlerBuildToolCallsJson(const LlmResponse*, char*, size_t);
typedef enum {
    SKILL_RULE_UNRESOLVED = 0,
    SKILL_RULE_BASE,
    SKILL_RULE_SELECT,
    SKILL_RULE_RESUME,
    SKILL_RULE_PROTOCOL_ERROR
} SkillRuleDecision;
typedef enum {
    SKILL_ROUTE_BASE = 0,
    SKILL_ROUTE_SELECT,
    SKILL_ROUTE_RESUME,
    SKILL_ROUTE_ERROR
} SkillRouteAction;
#define SKILL_ROUTE_NONE SKILL_ROUTE_BASE
typedef struct {
    SkillRouteAction action;
    char skillName[128], skillRunId[64], interruptionId[64];
    double confidence;
    char reasonCode[64], question[256], evidence[256], source[16];
} SkillRouterResult;
int SkillRouterInit(const CrabToolCatalogView*);
SkillRouterResult
SkillRouterRun(const char*, const char*, const char*, const char*, const char*, AgentTraceScope*);
int SkillRouterRecover(const char*, const char*, AgentTraceScope*, const char*, const char*);

int AgentLoopInit(const CrabRuntimeConfig*, const LlmConfig*, const MemoryConfig*);
int AgentLoopRefreshToolCatalog(void);
int AgentLoopStart(void);
void AgentLoopStop(void);
CrabRuntime* AgentLoopRuntime(void);

/* 直接运行一轮 (绕过消息总线, 供 agent_core 直接调用)
 * input: 用户输入文本
 * 返回: agent 回复 (malloc 分配, 调用者 free), NULL 失败
 */
char* run_round(const char* input, const char* sessionId,
                const char* requestId, const SkillRouterResult* route,
                AgentTraceScope* scope, const void* request);

/* tool_call 回调类型 (agent 把 LLM 返回的工具调用通过回调输出给舵机模块)
 * name: 工具名 (如 "robot.move")
 * args_json: 参数 JSON 字符串
 * result: 舵机执行结果输出缓冲区
 * result_max: result 缓冲区大小
 * 返回: 0 成功, 负数失败 (舵机执行结果填入 result)
 * 注: 设置后, agent 不调 CrabRuntimeCallTool, 而是通过回调输出 tool_call
 */
typedef int (*AgentToolCallCallback)(const char* name, const char* args_json,
                                     char* result, size_t result_max);
void AgentLoopSetToolCallCallback(AgentToolCallCallback cb);
#endif
