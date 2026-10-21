#ifndef LITECRAB_MEMORY_H
#define LITECRAB_MEMORY_H

#include "litecrab/runtime.h"
#include <stddef.h>

typedef struct {
    int enabled;
    char storeDir[CRAB_RUNTIME_PATH_MAX];
    int summaryMaxBytes;
    int recentMaxBytes;
    int recentContextItems;
    int maxContextBytes;
    int syncWrites;
} MemoryConfig;

void MemoryConfigDefaults(MemoryConfig*);
int MemoryConfigValid(const MemoryConfig*);
int MemoryInit(const MemoryConfig*, const char* workspaceRoot);
int MemoryIsEnabled(void);
int MemoryPathProtected(const char* path);
int MemoryBuildContext(const char* userId, char* out, size_t outSize);
int MemoryRemember(const char* userId, const char* category, const char* content, const char* source);
void MemoryShutdown(void);

#endif
