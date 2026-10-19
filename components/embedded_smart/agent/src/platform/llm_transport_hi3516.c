#include "litecrab/llm_transport.h"

#include <stdlib.h>

typedef struct {
    LlmTransportOps client;
} Hi3516Transport;

static int hi3516_post(void* context,
                       const LiteCrabHttpRequest* request,
                       char** responseBody) {
    Hi3516Transport* transport = context;
    return transport->client.post(transport->client.context, request, responseBody);
}

static void hi3516_cancel(void* context) {
    Hi3516Transport* transport = context;
    if (transport->client.cancel)
        transport->client.cancel(transport->client.context);
}

static void hi3516_destroy(void* context) {
    Hi3516Transport* transport = context;
    if (transport->client.destroy)
        transport->client.destroy(transport->client.context);
    free(transport);
}

int LlmHi3516TransportCreate(const LlmTransportOps* libllmclient, LlmTransportOps* output) {
    if (!libllmclient || !libllmclient->post || !output)
        return -1;
    Hi3516Transport* transport = calloc(1, sizeof *transport);
    if (!transport)
        return -1;
    transport->client = *libllmclient;
    *output = (LlmTransportOps){.context = transport,
                                .post = hi3516_post,
                                .cancel = hi3516_cancel,
                                .destroy = hi3516_destroy};
    return 0;
}
