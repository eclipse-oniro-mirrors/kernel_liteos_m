/* builtin_tools.c — 精简版 (机器人控制 agent)
 * 删除了所有文件操作工具 (read/write/edit/grep/glob/ls/pwd/exec/shell/csv_read)
 * 只保留 skill_read/skill_complete/memory_remember
 * 机器人工具 (robot.move/speak/stop) 在 agent_core.c 注册
 */
#include "litecrab/kernel.h"
#include "litecrab/runtime.h"
#include "litecrab/memory.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int raw_error(CrabRawResult* r, int code, const char* fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    r->exitCode = code;
    CrabRawResultSetStderr(r, b);
    return code;
}

/* ===== skill_read ===== */
static const CrabToolParamSpec sr_p[] = {
    {"skillPath", "skill name", CRAB_TOOL_VALUE_STRING, 1, 0, {0}, 0, 0, NULL, 0},
    {"fileName", "file name", CRAB_TOOL_VALUE_STRING, 0, 1, {0}, 0, 0, NULL, 0},
};
static int do_skill_read(CrabRuntime* r, const CrabToolCall* c, CrabRawResult* raw) {
    (void) r;
    const char *sp = CrabToolArgsGetString(c, "skillPath", NULL),
               *fn = CrabToolArgsGetString(c, "fileName", "SKILL.md");
    if (!sp || strstr(sp, "..") || sp[0] == '/' || strchr(sp, '\\') || strchr(sp, ':'))
        return raw_error(raw, CRAB_ERROR_INVALID_ARG, "invalid skill name");
    if (!AgentSessionStateSkillAllowed(sp))
        return raw_error(raw, CRAB_ERROR_PERMISSION, "skill was not selected by router");
    char* d = NULL;
    size_t n;
    int tr, rc = SkillReadContent(sp, fn, &d, &n, &tr);
    if (rc)
        return raw_error(raw, rc, "skill file not found");
    char *first = strstr(d, "---"), *second = first ? strstr(first + 3, "---") : NULL,
         *body = second ? second + 3 : d;
    while (*body && isspace((unsigned char) *body))
        body++;
    char* end = body + strlen(body);
    while (end > body && isspace((unsigned char) end[-1]))
        *--end = 0;
    raw->runtimeTruncated = tr;
    CrabRawResultSetStdout(raw, body);
    free(d);
    return 0;
}

/* ===== skill_complete ===== */
static const CrabToolParamSpec complete_p[] = {
    {"summary", "completion summary", CRAB_TOOL_VALUE_STRING, 0, 1, {0}, 0, 0, NULL, 0},
};
static int do_skill_complete(CrabRuntime* r, const CrabToolCall* c, CrabRawResult* raw) {
    (void) r;
    if (!SkillSupersetGetActive())
        return raw_error(raw, CRAB_ERROR_INVALID_ARG, "no active skill run");
    const char* summary = CrabToolArgsGetString(c, "summary", "completion requested");
    CrabRawResultSetStdout(raw, summary);
    return 0;
}

/* ===== builtin tools 注册表 ===== */
static const CrabToolSpec tools[] = {
    {"skill_read",
     "Load a skill's SKILL.md or referenced instruction file.",
     1, 1, 0,
     CRAB_TOOL_CONCURRENCY_SHARED_READ, CRAB_TOOL_RISK_LOW,
     sr_p, 2, do_skill_read, CrabBasicFilter},
    {"skill_complete",
     "Request completion of the active skill after its checks pass.",
     1, 1, 0,
     CRAB_TOOL_CONCURRENCY_SHARED_READ, CRAB_TOOL_RISK_LOW,
     complete_p, 1, do_skill_complete, CrabBasicFilter},
};

int CrabRuntimeRegisterBuiltinTools(CrabRuntime* r) {
    if (!r)
        return CRAB_ERROR_INVALID_ARG;
    for (size_t i = 0; i < sizeof tools / sizeof tools[0]; i++) {
        int rc = CrabRegistryRegister(&r->registry, &tools[i]);
        if (rc)
            return rc;
    }
    return 0;
}

/* ===== memory_remember ===== */
static int do_memory_remember(CrabRuntime* r, const CrabToolCall* c, CrabRawResult* raw) {
    (void) r;
    int result = MemoryRemember(AgentSessionStateGetUserId(),
                                CrabToolArgsGetString(c, "category", NULL),
                                CrabToolArgsGetString(c, "content", NULL), "agent");
    const char* message = result == 0 ? "{\"success\":true,\"code\":\"stored\",\"message\":\"memory stored\"}" :
        result == 1 ? "{\"success\":true,\"code\":\"duplicate\",\"message\":\"memory already exists\"}" :
        result == -3 ? "{\"success\":false,\"code\":\"memory_full\",\"message\":\"long-term memory is full\"}" :
        result == -1 ? "{\"success\":false,\"code\":\"invalid_input\",\"message\":\"memory was not saved\"}" :
        "{\"success\":false,\"code\":\"io_error\",\"message\":\"memory was not saved\"}";
    raw->exitCode = result == 0 || result == 1 ? 0 : 1;
    return CrabRawResultSetStdout(raw, message);
}

int CrabRuntimeRegisterMemoryTool(CrabRuntime* r) {
    static const char* categories[] = {"preference", "fact", "correction", "experience", "failure", "note"};
    static const CrabToolParamSpec params[] = {
        {.name = "category", .description = "memory category", .type = CRAB_TOOL_VALUE_STRING, .required = 1, .enumValues = categories, .enumCount = 6},
        {.name = "content", .description = "short memory statement", .type = CRAB_TOOL_VALUE_STRING, .required = 1}
    };
    static const CrabToolSpec spec = {"memory_remember",
        "Save a concise, safe user memory. Only explicit requests or stable verified information.",
        1, 0, 0, CRAB_TOOL_CONCURRENCY_GLOBAL_EXCLUSIVE, CRAB_TOOL_RISK_LOW,
        params, 2, do_memory_remember, CrabBasicFilter};
    return r ? CrabRegistryRegister(&r->registry, &spec) : CRAB_ERROR_INVALID_ARG;
}
