#include "litecrab/capability.h"
#include "litecrab/kernel.h"

static int agent_core_init(LiteCrabContext* context) {
    return AgentLoopInit(&context->runtimeConfig, &context->config.llm, &context->config.memory);
}

static int agent_core_start(LiteCrabContext* context) {
    (void) context;
    return AgentLoopStart();
}

static void agent_core_stop(LiteCrabContext* context) {
    (void) context;
    AgentLoopStop();
}

const LiteCrabCapabilityOps litecrab_cap_memory = {
    .id = "memory", .init = agent_core_init, .start = agent_core_start, .stop = agent_core_stop};
