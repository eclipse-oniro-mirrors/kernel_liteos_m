#include "litecrab/capability.h"
#include "litecrab/observability.h"

static int observability_init(LiteCrabContext* context) {
    return InitLogWithDir(context->config.logDir);
}

static void observability_destroy(LiteCrabContext* context) {
    (void) context;
    CloseLog();
}

const LiteCrabCapabilityOps litecrab_cap_observability = {
    .id = "observability", .init = observability_init, .destroy = observability_destroy};
