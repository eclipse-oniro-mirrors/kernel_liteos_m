/*
 * Copyright (c) 2022-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ItFuzzLosMemFreeDoubleFree: LOS_MemFree 同一指针二次释放
 *
 * 【现象】LOS_MemFree 同一指针两次触发 LOS_PANIC("node has been corrupted") 停机
 * （ws63），而非返回错误码。
 * 【根因】首次释放后节点 used 已为 NODE_UNUSED；二次释放在
 * LOSCFG_BASE_MEM_NODE_INTEGRITY_CHECK=y 时被完整性检查（los_heap.c
 * OsHeapIntegrityCheck）判为节点破坏直接 LOS_PANIC —— API 契约应返回 LOS_NOK，
 * 用例无法以断言看护只能隔离。
 * 【修复方向】双重释放路径改为返回错误码，不触发 LOS_PANIC。
 * 【恢复方法】修复后把 bug_isolate.h 中 FUZZ_BUG_S04_LOSMEM_DBLFREE 置 1，
 * 本用例恢复编译执行，作为回归看护用例常跑。
 */

#include "it_los_mem_fuzz.h"

static VOID __attribute__((unused)) FzTaskEntry(UINT32 arg)
{
    for (;;) { LOS_TaskDelay(10 + (arg & 0xF)); }
}

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();
    VOID *pool = (VOID *)(UINTPTR)OS_SYS_MEM_ADDR; VOID *ptr;
    for (int i = 0; i < 1; i++) {
        FuzzWatchdogFeed();
        ptr = LOS_MemAlloc(pool, 64);
        if (ptr != NULL) {
            (VOID)LOS_MemFree(pool, ptr);
            (VOID)LOS_MemFree(pool, ptr);
        }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}


VOID ItFuzzLosMemFreeDoubleFree(VOID)
{
    TEST_ADD_CASE("ItFuzzLosMemFreeDoubleFree", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL0, TEST_FUNCTION);
}
