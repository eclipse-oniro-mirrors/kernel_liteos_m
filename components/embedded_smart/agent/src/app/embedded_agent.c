#include "litecrab/agent.h"

#include "litecrab/hub.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct LiteCrabAgent {
    pthread_mutex_t executionMutex;
    int running;
};

static pthread_mutex_t ownerMutex = PTHREAD_MUTEX_INITIALIZER;
static LiteCrabAgent* activeAgent;

int LiteCrabAgentCreate(const LiteCrabAgentConfig* config, LiteCrabAgent** output) {
    if (!config || !output || !config->runtime.workspaceRoot)
        return LITECRAB_AGENT_INVALID_ARGUMENT;
    *output = NULL;
    LiteCrabAgent* agent = calloc(1, sizeof *agent);
    if (!agent || pthread_mutex_init(&agent->executionMutex, NULL)) {
        free(agent);
        return LITECRAB_AGENT_UNAVAILABLE;
    }
    pthread_mutex_lock(&ownerMutex);
    if (activeAgent) {
        pthread_mutex_unlock(&ownerMutex);
        pthread_mutex_destroy(&agent->executionMutex);
        free(agent);
        return LITECRAB_AGENT_BUSY;
    }
    activeAgent = agent;
    pthread_mutex_unlock(&ownerMutex);
    if (AgentLoopInit(&config->runtime, &config->llm, &config->memory) || AgentLoopStart()) {
        AgentLoopStop();
        pthread_mutex_lock(&ownerMutex);
        activeAgent = NULL;
        pthread_mutex_unlock(&ownerMutex);
        pthread_mutex_destroy(&agent->executionMutex);
        free(agent);
        return LITECRAB_AGENT_UNAVAILABLE;
    }
    agent->running = 1;
    *output = agent;
    return LITECRAB_AGENT_OK;
}

int LiteCrabAgentExecuteSync(LiteCrabAgent* agent,
                             const LiteCrabRequest* request,
                             LiteCrabResponse* response) {
    if (!agent || !request || !request->content || !response || !response->content ||
        !response->contentSize)
        return LITECRAB_AGENT_INVALID_ARGUMENT;
    if (pthread_mutex_trylock(&agent->executionMutex))
        return LITECRAB_AGENT_BUSY;
    if (!agent->running) {
        pthread_mutex_unlock(&agent->executionMutex);
        return LITECRAB_AGENT_UNAVAILABLE;
    }
    int timeout = request->timeoutMs > 0 ? request->timeoutMs : 30000;
    IngressOptions options = {.source = "embedded:api",
                              .userId = request->userId ? request->userId : "embedded",
                              .sessionId = request->sessionId,
                              .type = LITE_MSG_CHAT,
                              .priority = LITE_PRIORITY_NORMAL,
                              .replyMode = LITE_REPLY_SYNC,
                              .deadlineMs = LiteMonotonicMs() + timeout};
    IngressResult ingress;
    int result = IngressSubmit("embedded:api",
                               request->content,
                               (int) strlen(request->content),
                               &options,
                               &ingress);
    if (!result) {
        snprintf(response->requestId, sizeof response->requestId, "%s", ingress.requestId);
        result = DispatchResponseEx(ingress.requestId,
                                    LITE_REPLY_SYNC,
                                    timeout,
                                    response->content,
                                    (int) response->contentSize,
                                    &response->completionStatus);
    }
    pthread_mutex_unlock(&agent->executionMutex);
    return result ? LITECRAB_AGENT_TIMEOUT : LITECRAB_AGENT_OK;
}

int LiteCrabAgentCancel(LiteCrabAgent* agent, const char* requestId) {
    if (!agent || !requestId || !*requestId)
        return LITECRAB_AGENT_INVALID_ARGUMENT;
    return RequestCancel(requestId) ? LITECRAB_AGENT_UNAVAILABLE : LITECRAB_AGENT_OK;
}

void LiteCrabAgentDestroy(LiteCrabAgent* agent) {
    if (!agent)
        return;
    pthread_mutex_lock(&agent->executionMutex);
    if (agent->running) {
        RequestCancelAll();
        AgentLoopStop();
        agent->running = 0;
    }
    pthread_mutex_unlock(&agent->executionMutex);
    pthread_mutex_lock(&ownerMutex);
    if (activeAgent == agent)
        activeAgent = NULL;
    pthread_mutex_unlock(&ownerMutex);
    pthread_mutex_destroy(&agent->executionMutex);
    free(agent);
}
