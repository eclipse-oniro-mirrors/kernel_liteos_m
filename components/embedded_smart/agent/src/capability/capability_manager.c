#include "litecrab/capability.h"

#include <string.h>

static void destroy_initialized(LiteCrabCapabilityManager* manager, size_t limit) {
    while (limit > 0) {
        size_t index = --limit;
        if (!(manager->states[index] & LITECRAB_CAP_STATE_INITIALIZED) ||
            (manager->states[index] & LITECRAB_CAP_STATE_DESTROYED))
            continue;
        const LiteCrabCapabilityOps* ops = manager->capabilities[index];
        if (ops->destroy)
            ops->destroy(manager->context);
        manager->states[index] |= LITECRAB_CAP_STATE_DESTROYED;
    }
}

int LiteCrabCapabilityManagerInit(LiteCrabCapabilityManager* manager,
                                  LiteCrabContext* context,
                                  const LiteCrabCapabilityOps* const* capabilities,
                                  size_t count) {
    if (!manager || !context || (!capabilities && count) || count > LITECRAB_MAX_CAPABILITIES)
        return -1;
    memset(manager, 0, sizeof *manager);
    manager->context = context;
    manager->capabilities = capabilities;
    manager->count = count;
    for (size_t index = 0; index < count; index++) {
        const LiteCrabCapabilityOps* ops = capabilities[index];
        if (!ops || !ops->id || !ops->id[0] ||
            (!ops->init && !ops->start && !ops->quiesce && !ops->stop && !ops->destroy)) {
            manager->failedCapability = ops ? ops->id : NULL;
            destroy_initialized(manager, index);
            return -1;
        }
        for (size_t previous = 0; previous < index; previous++)
            if (!strcmp(ops->id, capabilities[previous]->id)) {
                manager->failedCapability = ops->id;
                destroy_initialized(manager, index);
                return -1;
            }
        if (ops->init && ops->init(context)) {
            manager->failedCapability = ops->id;
            destroy_initialized(manager, index);
            return -1;
        }
        manager->states[index] |= LITECRAB_CAP_STATE_INITIALIZED;
    }
    manager->initialized = 1;
    return 0;
}

int LiteCrabCapabilityManagerStart(LiteCrabCapabilityManager* manager) {
    if (!manager || !manager->initialized || manager->shutdown)
        return -1;
    if (manager->started)
        return 0;
    for (size_t index = 0; index < manager->count; index++) {
        const LiteCrabCapabilityOps* ops = manager->capabilities[index];
        if (ops->start && ops->start(manager->context)) {
            manager->failedCapability = ops->id;
            while (index > 0) {
                size_t previous = --index;
                if (!(manager->states[previous] & LITECRAB_CAP_STATE_STARTED))
                    continue;
                const LiteCrabCapabilityOps* started = manager->capabilities[previous];
                if (started->stop)
                    started->stop(manager->context);
                manager->states[previous] &= (unsigned char) ~LITECRAB_CAP_STATE_STARTED;
            }
            destroy_initialized(manager, manager->count);
            manager->shutdown = 1;
            return -1;
        }
        manager->states[index] |= LITECRAB_CAP_STATE_STARTED;
    }
    manager->started = 1;
    return 0;
}

int LiteCrabCapabilityManagerShutdown(LiteCrabCapabilityManager* manager) {
    if (!manager || !manager->initialized)
        return -1;
    if (manager->shutdown)
        return 0;
    int result = 0;
    for (size_t index = manager->count; index > 0; index--) {
        size_t current = index - 1;
        if (!(manager->states[current] & LITECRAB_CAP_STATE_STARTED))
            continue;
        const LiteCrabCapabilityOps* ops = manager->capabilities[current];
        if (ops->quiesce && ops->quiesce(manager->context))
            result = -1;
        manager->states[current] |= LITECRAB_CAP_STATE_QUIESCED;
    }
    for (size_t index = manager->count; index > 0; index--) {
        size_t current = index - 1;
        if (!(manager->states[current] & LITECRAB_CAP_STATE_STARTED))
            continue;
        const LiteCrabCapabilityOps* ops = manager->capabilities[current];
        if (ops->stop)
            ops->stop(manager->context);
        manager->states[current] &= (unsigned char) ~LITECRAB_CAP_STATE_STARTED;
    }
    destroy_initialized(manager, manager->count);
    manager->started = 0;
    manager->shutdown = 1;
    return result;
}

const char* LiteCrabCapabilityManagerFailedCapability(const LiteCrabCapabilityManager* manager) {
    return manager ? manager->failedCapability : NULL;
}
