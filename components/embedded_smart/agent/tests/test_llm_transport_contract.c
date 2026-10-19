#include "litecrab/kernel.h"
#include "litecrab/llm_transport.h"

#include <stdlib.h>
#include <string.h>

static int calls;

static int post(void* context, const LiteCrabHttpRequest* request, char** response) {
    (void) context;
    calls++;
    if (strcmp(request->host, "transport.invalid") || request->port != 443 || !request->useTls ||
        strcmp(request->path, "/v1/chat/completions") || !strstr(request->body, "litecrab-test"))
        return -201;
    const char* body =
        "{\"choices\":[{\"message\":{\"content\":\"transport-ok\"}}],"
        "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":1,\"total_tokens\":2}}";
    *response = malloc(strlen(body) + 1);
    if (!*response)
        return -200;
    strcpy(*response, body);
    return 0;
}

int main(void) {
    LlmConfig config = {.baseUrl = "https://transport.invalid/v1/chat/completions",
                        .apiKey = "test",
                        .model = "litecrab-test",
                        .timeoutMs = 1000};
    LlmTransportOps transport = {.post = post};
    if (LlmInit(&config) || LlmSetTransport(&transport))
        return 1;
    LlmRequestScopeBegin(0, 1);
    LlmResponse response;
    int result = LlmChatToolsEx("system", "[]", "[]", &response, 0);
    LlmRequestScopeEnd();
    LlmResetTransport();
    if (result || calls != 1 || strcmp(response.text, "transport-ok") || response.totalTokens != 2)
        return 1;
    LlmResponseClear(&response);
    return 0;
}
