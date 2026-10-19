#ifndef LITECRAB_EXTERNAL_TOOL_H
#define LITECRAB_EXTERNAL_TOOL_H

#include "litecrab/runtime.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int64_t deadlineMs;
} LiteCrabDeadline;

typedef struct {
    void* context;
    int (*invoke)(void* context,
                  uint64_t invocationId,
                  const char* toolId,
                  const char* argumentsJson,
                  char* resultJson,
                  size_t resultSize,
                  const LiteCrabDeadline* deadline);
    void (*cancel)(void* context, uint64_t invocationId);
} LiteCrabExternalToolBackend;

int LiteCrabExternalToolSetBackend(const LiteCrabExternalToolBackend* backend);
void LiteCrabExternalToolResetBackend(void);
int LiteCrabRegisterRobotTools(CrabRuntime* runtime);
size_t LiteCrabRobotActionCount(void);
size_t LiteCrabRobotToolsCount(void);

#endif
