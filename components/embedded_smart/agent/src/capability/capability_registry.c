#include "litecrab/capability.h"

#include "generated_capability_registry.inc"

const LiteCrabCapabilityOps* const* LiteCrabGeneratedCapabilityRegistry(size_t* count) {
    if (count)
        *count = LITECRAB_GENERATED_CAPABILITY_COUNT;
    return kLiteCrabGeneratedCapabilities;
}
