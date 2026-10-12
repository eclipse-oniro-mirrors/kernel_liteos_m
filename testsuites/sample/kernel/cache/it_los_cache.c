/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * Cache suite entry: registers every cache test case for the ws63
 * RISC-V arch/cache.h interface. Invoked from TestKernel() when
 * LOS_KERNEL_CACHE_TEST is 1.
 */

#include "it_los_cache.h"

VOID ItSuiteLosCache(VOID)
{
    ItLosCache001();
    ItLosCache002();
    ItLosCache003();
    ItLosCache004();
    ItLosCache005();
    ItLosCache006();
    ItLosCache007();
    ItLosCache008();
    ItLosCache009();
    ItLosCache010();
    ItLosCache011();
}
