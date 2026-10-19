#ifndef LITECRAB_MCP_CLIENT_H
#define LITECRAB_MCP_CLIENT_H

#include "litecrab/runtime.h"

#include <stddef.h>

#define LITECRAB_MCP_MAX_DEVICES 8
#define LITECRAB_MCP_DEVICE_ID_MAX 64
#define LITECRAB_MCP_ALIAS_MAX 64
#define LITECRAB_MCP_URL_MAX 768
#define LITECRAB_MCP_PATH_MAX 512
#define LITECRAB_MCP_ENV_NAME_MAX 128

typedef struct {
    int enabled;
    int required;
    char id[LITECRAB_MCP_DEVICE_ID_MAX];
    char alias[LITECRAB_MCP_ALIAS_MAX];
    char url[LITECRAB_MCP_URL_MAX];
    char caFile[LITECRAB_MCP_PATH_MAX];
    char clientCertFile[LITECRAB_MCP_PATH_MAX];
    char clientKeyFile[LITECRAB_MCP_PATH_MAX];
    char tokenEnv[LITECRAB_MCP_ENV_NAME_MAX];
} McpDeviceConfig;

typedef struct {
    int enabled;
    int required;
    int connectTimeoutMs;
    int requestTimeoutMs;
    int maxMessageBytes;
    int deviceCount;
    McpDeviceConfig devices[LITECRAB_MCP_MAX_DEVICES];
} McpClientConfig;

typedef struct McpClientManager McpClientManager;

int McpClientManagerCreate(McpClientManager** out, const McpClientConfig* config);
int McpClientManagerStart(McpClientManager* manager,
                          CrabRuntime* runtime,
                          char* error,
                          size_t errorSize);
void McpClientManagerStop(McpClientManager* manager);
void McpClientManagerDestroy(McpClientManager* manager);
int McpClientManagerReadyDevices(const McpClientManager* manager);
int McpClientManagerRegisteredTools(const McpClientManager* manager);

#endif
