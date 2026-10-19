#include "litecrab/capability.h"

#include <stdio.h>
#include <string.h>

static char events[256];
static size_t eventLength;
static const char* failStart;

static void record(const char* event) {
    int written = snprintf(events + eventLength,
                           sizeof events - eventLength,
                           "%s%s",
                           eventLength ? "," : "",
                           event);
    if (written > 0)
        eventLength += (size_t) written;
}

#define DEFINE_CAP(name)                                                                           \
    static int name##_init(LiteCrabContext* context) {                                              \
        (void) context;                                                                             \
        record(#name ".init");                                                                     \
        return 0;                                                                                   \
    }                                                                                               \
    static int name##_start(LiteCrabContext* context) {                                             \
        (void) context;                                                                             \
        record(#name ".start");                                                                    \
        return failStart && !strcmp(failStart, #name) ? -1 : 0;                                    \
    }                                                                                               \
    static int name##_quiesce(LiteCrabContext* context) {                                          \
        (void) context;                                                                             \
        record(#name ".quiesce");                                                                 \
        return 0;                                                                                   \
    }                                                                                               \
    static void name##_stop(LiteCrabContext* context) {                                             \
        (void) context;                                                                             \
        record(#name ".stop");                                                                     \
    }                                                                                               \
    static void name##_destroy(LiteCrabContext* context) {                                          \
        (void) context;                                                                             \
        record(#name ".destroy");                                                                  \
    }                                                                                               \
    static const LiteCrabCapabilityOps name = {                                                     \
        #name, name##_init, name##_start, name##_quiesce, name##_stop, name##_destroy}

DEFINE_CAP(alpha);
DEFINE_CAP(beta);
DEFINE_CAP(gamma);

static int check(int condition, const char* message) {
    if (condition)
        return 0;
    fprintf(stderr, "FAIL: %s; events=%s\n", message, events);
    return 1;
}

int main(void) {
    LiteCrabContext context;
    LiteCrabCapabilityManager manager;
    const LiteCrabCapabilityOps* all[] = {&alpha, &beta, &gamma};
    memset(&context, 0, sizeof context);

    int failures = 0;
    failures += check(!LiteCrabCapabilityManagerInit(&manager, &context, all, 3), "init");
    failures += check(!LiteCrabCapabilityManagerStart(&manager), "start");
    failures += check(!LiteCrabCapabilityManagerShutdown(&manager), "shutdown");
    failures += check(!LiteCrabCapabilityManagerShutdown(&manager), "idempotent shutdown");
    failures += check(
        !strcmp(events,
                "alpha.init,beta.init,gamma.init,alpha.start,beta.start,gamma.start,"
                "gamma.quiesce,beta.quiesce,alpha.quiesce,gamma.stop,beta.stop,alpha.stop,"
                "gamma.destroy,beta.destroy,alpha.destroy"),
        "normal order");

    events[0] = 0;
    eventLength = 0;
    failStart = "beta";
    failures += check(!LiteCrabCapabilityManagerInit(&manager, &context, all, 3), "failure init");
    failures += check(LiteCrabCapabilityManagerStart(&manager) != 0, "failure start");
    failures += check(
        !strcmp(events,
                "alpha.init,beta.init,gamma.init,alpha.start,beta.start,alpha.stop,"
                "gamma.destroy,beta.destroy,alpha.destroy"),
        "start rollback order");
    failures += check(!strcmp(LiteCrabCapabilityManagerFailedCapability(&manager), "beta"),
                      "failed capability");

    LiteCrabCapabilityOps duplicate = alpha;
    const LiteCrabCapabilityOps* duplicates[] = {&alpha, &duplicate};
    events[0] = 0;
    eventLength = 0;
    failStart = NULL;
    failures += check(LiteCrabCapabilityManagerInit(&manager, &context, duplicates, 2) != 0,
                      "duplicate ID rejection");
    failures += check(!strcmp(events, "alpha.init,alpha.destroy"), "duplicate rollback");

    LiteCrabCapabilityOps empty = {.id = "empty"};
    const LiteCrabCapabilityOps* invalid[] = {&empty};
    failures += check(LiteCrabCapabilityManagerInit(&manager, &context, invalid, 1) != 0,
                      "empty capability rejection");
    return failures ? 1 : 0;
}
