#include "../builtin_tools_internal.h"

#include "litecrab/kernel.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int CrabBuiltinSkillRead(CrabRuntime* r, const CrabToolCall* c, CrabRawResult* raw) {
    (void) r;
    const char *sp = CrabToolArgsGetString(c, "skillPath", NULL),
               *fn = CrabToolArgsGetString(c, "fileName", "SKILL.md");
    if (!sp || strstr(sp, "..") || sp[0] == '/' || strchr(sp, '\\') || strchr(sp, ':'))
        return CrabBuiltinRawError(raw, CRAB_ERROR_INVALID_ARG, "invalid skill name");
    if (!AgentSessionStateSkillAllowed(sp))
        return CrabBuiltinRawError(raw, CRAB_ERROR_PERMISSION, "skill was not selected by router");
    char* d = NULL;
    size_t n;
    int tr, rc = SkillReadContent(sp, fn, &d, &n, &tr);
    if (rc)
        return CrabBuiltinRawError(raw, rc, "skill file not found");
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

int CrabBuiltinSkillComplete(CrabRuntime* r, const CrabToolCall* c, CrabRawResult* raw) {
    (void) r;
    if (!SkillSupersetGetActive())
        return CrabBuiltinRawError(raw, CRAB_ERROR_INVALID_ARG, "no active skill run");
    const char* summary = CrabToolArgsGetString(c, "summary", "completion requested");
    CrabRawResultSetStdout(raw, summary);
    return 0;
}
