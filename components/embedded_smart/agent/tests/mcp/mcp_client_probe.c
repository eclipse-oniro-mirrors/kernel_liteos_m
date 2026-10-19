#include "litecrab/mcp_client.h"
#include "litecrab/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int call_no_args(CrabRuntime* runtime, const char* name, char* output, size_t outputSize) {
    CrabToolCall call = {0};
    CrabToolResponse response = {0};
    snprintf(call.callId, sizeof call.callId, "probe-1");
    snprintf(call.toolName, sizeof call.toolName, "%s", name);
    int rc = CrabRuntimeCallTool(runtime, &call, &response);
    snprintf(output, outputSize, "%s", response.content);
    return rc || !response.success ? -1 : 0;
}

static int call_echo(CrabRuntime* runtime) {
    CrabToolCall call = {0};
    CrabToolResponse response = {0};
    snprintf(call.callId, sizeof call.callId, "probe-echo");
    snprintf(call.toolName, sizeof call.toolName, "lab__device_echo");
    snprintf(call.args[0].name, sizeof call.args[0].name, "message");
    call.args[0].value.type = CRAB_TOOL_VALUE_STRING;
    call.args[0].value.stringValue = "hello MCP 世界";
    call.argCount = 1;
    int rc = CrabRuntimeCallTool(runtime, &call, &response);
    return rc || !response.success || !strstr(response.content, "hello MCP 世界") ? -1 : 0;
}

static int call_add(CrabRuntime* runtime) {
    CrabToolCall call = {0};
    CrabToolResponse response = {0};
    snprintf(call.callId, sizeof call.callId, "probe-add");
    snprintf(call.toolName, sizeof call.toolName, "lab__device_add");
    snprintf(call.args[0].name, sizeof call.args[0].name, "a");
    call.args[0].value.type = CRAB_TOOL_VALUE_INT;
    call.args[0].value.intValue = 19;
    snprintf(call.args[1].name, sizeof call.args[1].name, "b");
    call.args[1].value.type = CRAB_TOOL_VALUE_INT;
    call.args[1].value.intValue = 23;
    call.argCount = 2;
    int rc = CrabRuntimeCallTool(runtime, &call, &response);
    return rc || !response.success || !strstr(response.content, "42") ? -1 : 0;
}

static int call_timeout(CrabRuntime* runtime) {
    CrabToolCall call = {0};
    CrabToolResponse response = {0};
    snprintf(call.callId, sizeof call.callId, "probe-delay");
    snprintf(call.toolName, sizeof call.toolName, "lab__device_delay");
    snprintf(call.args[0].name, sizeof call.args[0].name, "milliseconds");
    call.args[0].value.type = CRAB_TOOL_VALUE_INT;
    call.args[0].value.intValue = 500;
    call.argCount = 1;
    int rc = CrabRuntimeCallTool(runtime, &call, &response);
    return rc || response.success || !strstr(response.content, "Timeout") ? -1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s URL [--optional-offline|--timeout-test]\n", argv[0]);
        return 2;
    }
    int optionalOffline = argc > 2 && !strcmp(argv[2], "--optional-offline");
    int timeoutTest = argc > 2 && !strcmp(argv[2], "--timeout-test");
    McpClientConfig config = {0};
    config.enabled = 1;
    config.required = !optionalOffline;
    config.connectTimeoutMs = optionalOffline ? 100 : 1000;
    config.requestTimeoutMs = timeoutTest ? 100 : 2000;
    config.maxMessageBytes = 256 * 1024;
    config.deviceCount = 1;
    config.devices[0].enabled = 1;
    config.devices[0].required = !optionalOffline;
    snprintf(config.devices[0].id, sizeof config.devices[0].id, "test-device");
    snprintf(config.devices[0].alias, sizeof config.devices[0].alias, "lab");
    snprintf(config.devices[0].url, sizeof config.devices[0].url, "%s", argv[1]);
    if (getenv("MCP_TEST_USE_TOKEN"))
        snprintf(config.devices[0].tokenEnv,
                 sizeof config.devices[0].tokenEnv,
                 "MCP_TEST_TOKEN");
    CrabRuntime runtime;
    CrabRuntimeConfig runtimeConfig = {.workspaceRoot = "."};
    if (CrabRuntimeInit(&runtime, &runtimeConfig))
        return 3;
    McpClientManager* manager = NULL;
    if (McpClientManagerCreate(&manager, &config))
        return 4;
    char error[512] = "";
    int started = McpClientManagerStart(manager, &runtime, error, sizeof error);
    if (optionalOffline) {
        int ok = !started && McpClientManagerReadyDevices(manager) == 0 &&
                 McpClientManagerRegisteredTools(manager) == 0;
        McpClientManagerDestroy(manager);
        CrabRuntimeDestroy(&runtime);
        if (!ok) {
            fprintf(stderr, "optional offline behavior failed: %s\n", error);
            return 5;
        }
        puts("optional-offline: ok");
        return 0;
    }
    if (started) {
        fprintf(stderr, "MCP start failed: %s\n", error);
        McpClientManagerDestroy(manager);
        return 6;
    }
    if (McpClientManagerReadyDevices(manager) != 1 ||
        McpClientManagerRegisteredTools(manager) != 4) {
        fprintf(stderr,
                "unexpected catalog devices=%d tools=%d\n",
                McpClientManagerReadyDevices(manager),
                McpClientManagerRegisteredTools(manager));
        McpClientManagerDestroy(manager);
        return 7;
    }
    char status[4096];
    int failed = call_no_args(&runtime, "lab__device_get_status", status, sizeof status) ||
                 call_echo(&runtime) || call_add(&runtime);
    if (timeoutTest)
        failed = failed || call_timeout(&runtime);
    McpClientManagerStop(manager);
    char stopped[4096];
    if (!call_no_args(&runtime, "lab__device_get_status", stopped, sizeof stopped))
        failed = 1;
    McpClientManagerDestroy(manager);
    CrabRuntimeDestroy(&runtime);
    if (failed) {
        fprintf(stderr, "MCP probe failed\n");
        return 8;
    }
    puts("mcp-client-probe: ok");
    return 0;
}
