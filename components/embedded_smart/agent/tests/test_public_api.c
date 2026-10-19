#include "litecrab/alarm.h"
#include "litecrab/alarm_app.h"
#include "litecrab/config.h"
#include "litecrab/gateway.h"
#include "litecrab/hub.h"
#include "litecrab/json.h"
#include "litecrab/kernel.h"
#include "litecrab/mcp_client.h"
#include "litecrab/memory.h"
#include "litecrab/observability.h"
#include "litecrab/runtime.h"
#include "litecrab/session.h"
#include "litecrab/working_memory.h"

#include <string.h>

int main(void) {
    CrabRuntime runtime;
    CrabRuntimeConfig runtime_config;
    LiteCrabAppConfig app_config;
    AlarmApp alarm_app;

    memset(&runtime, 0, sizeof runtime);
    memset(&runtime_config, 0, sizeof runtime_config);
    memset(&app_config, 0, sizeof app_config);
    memset(&alarm_app, 0, sizeof alarm_app);
    return runtime.registry.toolCount || runtime_config.workspaceRoot ||
                   app_config.workspaceRoot[0] || alarm_app.service
               ? 1
               : 0;
}
