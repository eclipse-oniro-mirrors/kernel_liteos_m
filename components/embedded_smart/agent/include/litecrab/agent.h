#ifndef LITECRAB_AGENT_H
#define LITECRAB_AGENT_H

#include "litecrab/kernel.h"
#include "litecrab/hub.h"

#include <stddef.h>
#include <stdint.h>

typedef struct LiteCrabAgent LiteCrabAgent;

typedef struct {
    CrabRuntimeConfig runtime;
    LlmConfig llm;
    MemoryConfig memory;
} LiteCrabAgentConfig;

typedef struct {
    uint64_t requestId;
    const char* content;
    const char* userId;
    const char* sessionId;
    int timeoutMs;
} LiteCrabRequest;

typedef struct {
    char* content;
    size_t contentSize;
    char requestId[64];
    LiteCompletionStatus completionStatus;
} LiteCrabResponse;

enum {
    LITECRAB_AGENT_OK = 0,
    LITECRAB_AGENT_INVALID_ARGUMENT = -3001,
    LITECRAB_AGENT_BUSY = -3002,
    LITECRAB_AGENT_UNAVAILABLE = -3003,
    LITECRAB_AGENT_TIMEOUT = -3004
};

int LiteCrabAgentCreate(const LiteCrabAgentConfig* config, LiteCrabAgent** output);
int LiteCrabAgentExecuteSync(LiteCrabAgent* agent,
                             const LiteCrabRequest* request,
                             LiteCrabResponse* response);
int LiteCrabAgentCancel(LiteCrabAgent* agent, const char* requestId);
void LiteCrabAgentDestroy(LiteCrabAgent* agent);

#endif
