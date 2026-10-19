#include "litecrab/capability.h"
#include "litecrab/kernel.h"
#include "litecrab/observability.h"

#include <stdio.h>

static int mcp_init(LiteCrabContext* context) {
    if (McpClientManagerCreate(&context->mcp, &context->config.mcp)) {
        fprintf(stderr, "failed to allocate MCP Client Manager\n");
        return -1;
    }
    return 0;
}

static int mcp_start(LiteCrabContext* context) {
    char error[512] = "";
    if (McpClientManagerStart(context->mcp, AgentLoopRuntime(), error, sizeof error) ||
        AgentLoopRefreshToolCatalog()) {
        fprintf(stderr, "MCP startup failed: %s\n", error);
        return -1;
    }
    if (context->config.mcp.enabled)
        LogPrint("[mcp] started ready_devices=%d registered_tools=%d status=%s",
                 McpClientManagerReadyDevices(context->mcp),
                 McpClientManagerRegisteredTools(context->mcp),
                 error[0] ? error : "ready");
    return 0;
}

static void mcp_stop(LiteCrabContext* context) {
    McpClientManagerStop(context->mcp);
}

static void mcp_destroy(LiteCrabContext* context) {
    McpClientManagerDestroy(context->mcp);
    context->mcp = NULL;
}

const LiteCrabCapabilityOps litecrab_cap_mcp = {
    .id = "mcp_client", .init = mcp_init, .start = mcp_start, .stop = mcp_stop, .destroy = mcp_destroy};
