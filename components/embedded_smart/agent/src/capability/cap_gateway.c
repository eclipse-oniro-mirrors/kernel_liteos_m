#include "litecrab/capability.h"
#include "litecrab/gateway.h"
#include "litecrab/kernel.h"

#include <time.h>

static void* gateway_main(void* argument) {
    LiteCrabContext* context = argument;
    context->gatewayResult =
        StartRequestServer(&context->config.server, LiteCrabHandleNetworkRequest, NULL);
    atomic_store(&context->gatewayFinished, 1);
    return NULL;
}

static int gateway_start(LiteCrabContext* context) {
    atomic_store(&context->gatewayFinished, 0);
    context->gatewayResult = 0;
    if (pthread_create(&context->gatewayThread, NULL, gateway_main, context))
        return -1;
    atomic_store(&context->gatewayThreadStarted, 1);
    struct timespec startup = {.tv_sec = 0, .tv_nsec = 100000000L};
    nanosleep(&startup, NULL);
    if (atomic_load(&context->gatewayFinished)) {
        pthread_join(context->gatewayThread, NULL);
        atomic_store(&context->gatewayThreadStarted, 0);
        return context->gatewayResult ? context->gatewayResult : -1;
    }
    return 0;
}

static int gateway_quiesce(LiteCrabContext* context) {
    (void) context;
    RequestServerRequestStop();
    LlmCancelActiveRequests();
    return 0;
}

static void gateway_stop(LiteCrabContext* context) {
    RequestServerRequestStop();
    if (atomic_exchange(&context->gatewayThreadStarted, 0))
        pthread_join(context->gatewayThread, NULL);
}

const LiteCrabCapabilityOps litecrab_cap_gateway = {.id = "tcp_gateway",
                                                     .start = gateway_start,
                                                     .quiesce = gateway_quiesce,
                                                     .stop = gateway_stop};
