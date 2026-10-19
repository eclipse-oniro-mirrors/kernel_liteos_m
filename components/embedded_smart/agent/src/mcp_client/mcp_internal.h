#ifndef LITECRAB_MCP_INTERNAL_H
#define LITECRAB_MCP_INTERNAL_H

#include "litecrab/mcp_client.h"

#include <stddef.h>

#define MCP_PROTOCOL_MODERN "2026-07-28"
#define MCP_PROTOCOL_LEGACY "2025-11-25"
#define MCP_CLIENT_MAX_TOOLS_PER_DEVICE 16
#define MCP_CLIENT_MAX_PARAMS_PER_TOOL 8
#define MCP_CLIENT_PUBLIC_NAME_MAX 128
#define MCP_CLIENT_DESCRIPTION_MAX 512
#define MCP_CLIENT_SESSION_MAX 128

typedef struct {
    long status;
    char* body;
    size_t bodySize;
    char contentType[96];
    char sessionId[MCP_CLIENT_SESSION_MAX];
    int overflow;
} McpHttpResponse;

int McpHttpPost(const McpDeviceConfig* device,
                const McpClientConfig* config,
                const char* bearerToken,
                const char* protocolVersion,
                const char* method,
                const char* resourceName,
                const char* sessionId,
                const char* body,
                McpHttpResponse* response,
                char* error,
                size_t errorSize);
void McpHttpResponseClear(McpHttpResponse* response);

int McpBuildDiscoverRequest(unsigned long long id, char* out, size_t outSize);
int McpBuildInitializeRequest(unsigned long long id, char* out, size_t outSize);
int McpBuildInitializedNotification(char* out, size_t outSize);
int McpBuildListToolsRequest(const char* version,
                             unsigned long long id,
                             char* out,
                             size_t outSize);
int McpBuildCallToolRequest(const char* version,
                            unsigned long long id,
                            const char* toolName,
                            const CrabToolCall* call,
                            char* out,
                            size_t outSize);
int McpResponseHasError(const char* json, char* message, size_t messageSize);
int McpResponseIdMatches(const char* json, unsigned long long expectedId);
int McpDiscoverSupportsVersion(const char* json, const char* version);
int McpInitializeVersionMatches(const char* json, const char* version);

#endif
