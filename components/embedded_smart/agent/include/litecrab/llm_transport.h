#ifndef LITECRAB_LLM_TRANSPORT_H
#define LITECRAB_LLM_TRANSPORT_H

#include <stddef.h>

typedef struct {
    const char* host;
    int port;
    const char* path;
    int useTls;
    const char* apiKey;
    const char* body;
    int timeoutMs;
    void* cancelContext;
    int (*isCancelled)(void* context);
} LiteCrabHttpRequest;

typedef struct {
    void* context;
    int (*post)(void* context, const LiteCrabHttpRequest* request, char** responseBody);
    void (*cancel)(void* context);
    void (*destroy)(void* context);
} LlmTransportOps;

int LlmSetTransport(const LlmTransportOps* transport);
void LlmResetTransport(void);
int LlmOpenSslTransportPost(const LiteCrabHttpRequest* request, char** responseBody);
int LlmHi3516TransportCreate(const LlmTransportOps* libllmclient, LlmTransportOps* output);

#endif
