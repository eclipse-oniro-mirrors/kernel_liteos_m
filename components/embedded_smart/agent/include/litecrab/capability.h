#ifndef LITECRAB_CAPABILITY_H
#define LITECRAB_CAPABILITY_H

#include "litecrab/alarm_app.h"
#include "litecrab/config.h"
#include "litecrab/mcp_client.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>

#define LITECRAB_MAX_CAPABILITIES 32

typedef struct LiteCrabContext {
    LiteCrabAppConfig config;
    CrabRuntimeConfig runtimeConfig;
    AlarmAppOptions alarmOptions;
    AlarmApp alarm;
    McpClientManager* mcp;
    pthread_t gatewayThread;
    atomic_int gatewayThreadStarted;
    atomic_int gatewayFinished;
    int gatewayResult;
} LiteCrabContext;

typedef struct {
    const char* id;
    int (*init)(LiteCrabContext* context);
    int (*start)(LiteCrabContext* context);
    int (*quiesce)(LiteCrabContext* context);
    void (*stop)(LiteCrabContext* context);
    void (*destroy)(LiteCrabContext* context);
} LiteCrabCapabilityOps;

typedef enum {
    LITECRAB_CAP_STATE_NONE = 0,
    LITECRAB_CAP_STATE_INITIALIZED = 1,
    LITECRAB_CAP_STATE_STARTED = 2,
    LITECRAB_CAP_STATE_QUIESCED = 4,
    LITECRAB_CAP_STATE_DESTROYED = 8
} LiteCrabCapabilityState;

typedef struct {
    LiteCrabContext* context;
    const LiteCrabCapabilityOps* const* capabilities;
    size_t count;
    unsigned char states[LITECRAB_MAX_CAPABILITIES];
    const char* failedCapability;
    int initialized;
    int started;
    int shutdown;
} LiteCrabCapabilityManager;

int LiteCrabCapabilityManagerInit(LiteCrabCapabilityManager* manager,
                                  LiteCrabContext* context,
                                  const LiteCrabCapabilityOps* const* capabilities,
                                  size_t count);
int LiteCrabCapabilityManagerStart(LiteCrabCapabilityManager* manager);
int LiteCrabCapabilityManagerShutdown(LiteCrabCapabilityManager* manager);
const char* LiteCrabCapabilityManagerFailedCapability(const LiteCrabCapabilityManager* manager);

const LiteCrabCapabilityOps* const* LiteCrabGeneratedCapabilityRegistry(size_t* count);

#endif
