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
#if defined(KERNEL_BUG_13_CACHE_MAINT_LAYOUT_FREEZE) && KERNEL_BUG_13_CACHE_MAINT_LAYOUT_FREEZE == 1
    /* #78 布局敏感缺陷: 触发点随构建布局漂移(002 单批布局冻 009, 合批布局冻 011),
     * 维护族 009-011 一并隔离; 001-008 全部布局均未触发, 照常执行。修复后置 1 恢复 */
    ItLosCache009();
    ItLosCache010();
    ItLosCache011();
#endif
#if defined(KERNEL_BUG_13_CACHE_MAINT_PHASE_FREEZE) && KERNEL_BUG_13_CACHE_MAINT_PHASE_FREEZE == 1
    ItLosCache012(); /* #78 缺陷探针: 修复后置 1 恢复(见 bug_isolate.h) */
#endif
}
