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
    /* ItLosCache012: #78 缺陷探针。其 A 段在任务运行中调用全量
     * ArchDCacheInvalidate()(纯 DCIV 不回写), 会丢弃任务栈脏行导致
     * ra 损坏→非法指令异常。这是用例设计缺陷(假设失效不丢脏行已被
     * 证伪), 非 cache 实现问题。fbb_ws63 cpu_cache_init 在启动早期
     * (无任务上下文)调用全量 invalidate 是安全的, 任务运行中不安全。 */
    /* 013-017: 新增接口覆盖/边界/功能完备度用例(源码化后回归补强) */
    ItLosCache013();
    ItLosCache014();
    ItLosCache015();
    ItLosCache016();
    ItLosCache017();
}
