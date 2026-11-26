/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* 用例简要描述: ItSmpLosMem005 Realloc/AllocAlign 跨核  */
#include "osTest.h"
#include "It_los_mem.h"
#include "los_memory.h"
#ifdef LOSCFG_KERNEL_SMP

#define SMP_MEM_ROUNDS 300
/* Realloc/AllocAlign 跨核 */
static volatile INT32 g_done5 = 0;
static volatile UINT32 g_err5 = 0;

static void MemF05(void)
{
    int i;
    for (i = 0; i < 100; i++) {
        VOID *p = LOS_MemAllocAlign((VOID *)OS_SYS_MEM_ADDR, 64, 32);
        if (p == NULL) { g_err5 = g_err5 + 1; continue; }
        if (((UINTPTR)p % 32) != 0) { g_err5 = g_err5 + 1; }
        (void)LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, p);
    }
    LOS_AtomicInc((Atomic *)&g_done5);
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S t;
    UINT32 taskId;
    VOID *p;
    UINT32 used0, used1;
    int i;

    (void)memset_s(&t, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)MemF05;
    t.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    t.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    t.pcName = "smp_mem_005";
    t.usCpuAffiMask = CPUID_TO_AFFI_MASK((ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM);
    g_done5 = 0;
    g_err5 = 0;
    ICUNIT_ASSERT_EQUAL(LOS_TaskCreate(&taskId, &t), LOS_OK, LOS_TaskCreate(&taskId, &t));

    for (i = 0; i < 100; i++) {
        p = LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, 64);
        if (p == NULL) { g_err5 = g_err5 + 1; continue; }
        p = LOS_MemRealloc((VOID *)OS_SYS_MEM_ADDR, p, 128);
        if (p == NULL) { g_err5 = g_err5 + 1; continue; }
        (void)LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, p);
    }
    {
        UINT64 tEnd = TestTickCountGet() + 300;
        while (g_done5 == 0 && TestTickCountGet() < tEnd) { }
    }
    {   /* 帮手退出后取基线单核验证守恒 */
        UINT64 tEnd = TestTickCountGet() + 50;
        while (TestTickCountGet() < tEnd) { }
        used0 = LOS_MemTotalUsedGet((VOID *)OS_SYS_MEM_ADDR);
        for (i = 0; i < 100; i++) {
            p = LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, 64);
            if (p == NULL) { g_err5 = g_err5 + 1; continue; }
            p = LOS_MemRealloc((VOID *)OS_SYS_MEM_ADDR, p, 128);
            if (p == NULL) { g_err5 = g_err5 + 1; continue; }
            (void)LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, p);
        }
        used1 = LOS_MemTotalUsedGet((VOID *)OS_SYS_MEM_ADDR);
    }
    ICUNIT_ASSERT_EQUAL(g_done5, 1, g_done5);
    ICUNIT_ASSERT_EQUAL(g_err5, 0, g_err5);
    ICUNIT_ASSERT_EQUAL(used1, used0, used1 - used0);
    return LOS_OK;
}
VOID ItSmpLosMem005(void)
{
    TEST_ADD_CASE("ItSmpLosMem005", Testcase, TEST_LOS, TEST_FN_KERNEL_DYNMEM, TEST_LEVEL2, TEST_FUNCTION);
}
#endif

