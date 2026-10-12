/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_mem.h"

#ifdef LOSCFG_KERNEL_MEM_BESTFIT

#define MEM_TC325_POOL_SIZE     0x1000

STATIC UINT8 g_memPoolTC325[MEM_TC325_POOL_SIZE] __attribute__((aligned(64)));

STATIC volatile UINT32 g_tc325HookCount = 0;

STATIC VOID MemTc325MallocHook(VOID)
{
    g_tc325HookCount++;
}

/* 用例简要描述: malloc挂钩后分配触发hook一次 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 countBefore;
    VOID *ptr = NULL;
    VOID *ptr2 = NULL;
    MALLOC_HOOK oldHook = g_MALLOC_HOOK;

    ret = LOS_MemPoolInit(g_memPoolTC325, MEM_TC325_POOL_SIZE, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 公开全局 g_MALLOC_HOOK 挂 static hook 后分配 -> 每次分配触发一次
     * (bestfit los_memory.c:1605-1608), 计数差值==1 */
    g_MALLOC_HOOK = MemTc325MallocHook;
    countBefore = g_tc325HookCount;
    ptr = LOS_MemAlloc(g_memPoolTC325, 0x10);
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);
    ICUNIT_GOTO_EQUAL(g_tc325HookCount - countBefore, 1, g_tc325HookCount - countBefore, EXIT);

    /* 置 NULL 恢复后计数不再增长, 分配/释放正常 */
    g_MALLOC_HOOK = NULL;
    countBefore = g_tc325HookCount;
    ptr2 = LOS_MemAlloc(g_memPoolTC325, 0x10);
    ICUNIT_GOTO_NOT_EQUAL(ptr2, NULL, 0, EXIT);
    ICUNIT_GOTO_EQUAL(g_tc325HookCount - countBefore, 0, g_tc325HookCount - countBefore, EXIT);

    ret = LOS_MemFree(g_memPoolTC325, ptr);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ptr = NULL;
    ret = LOS_MemFree(g_memPoolTC325, ptr2);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ptr2 = NULL;

    ret = LOS_MemDeInit(g_memPoolTC325);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    g_MALLOC_HOOK = oldHook; /* 恢复入口原值(NULL) */
    return LOS_OK;

EXIT:
    if (ptr != NULL) {
        (VOID)LOS_MemFree(g_memPoolTC325, ptr);
    }
    if (ptr2 != NULL) {
        (VOID)LOS_MemFree(g_memPoolTC325, ptr2);
    }
    (VOID)LOS_MemDeInit(g_memPoolTC325);
    g_MALLOC_HOOK = oldHook; /* 失败路径也恢复 hook 为入口原值 */
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_BESTFIT */

VOID ItLosMem325(VOID)
{
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    TEST_ADD_CASE("ItLosMem325", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL0, TEST_FUNCTION);
#endif
}
