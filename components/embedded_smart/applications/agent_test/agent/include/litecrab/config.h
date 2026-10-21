#ifndef LITECRAB_CONFIG_H
#define LITECRAB_CONFIG_H
#include "litecrab/kernel.h"
#include "litecrab/memory.h"

typedef struct {
    char workspaceRoot[CRAB_RUNTIME_PATH_MAX];
    char logDir[512];
    char llmBaseUrl[768];
    char llmApiKey[512];
    char llmModel[256];
    char reasoningEffort[64];
    LlmConfig llm;
    MemoryConfig memory;
} LiteCrabAppConfig;

void LiteCrabConfigDefaults(LiteCrabAppConfig* config);
int LiteCrabConfigLoad(LiteCrabAppConfig* config,
                       const char* baseConfigPath,
                       const char* llmConfigPath,
                       char* error,
                       size_t errorSize);
int LiteCrabConfigLoadProvider(LiteCrabAppConfig* config,
                               const char* baseConfigPath,
                               const char* llmConfigPath,
                               const char* llmProvider,
                               char* error,
                               size_t errorSize);

#endif
