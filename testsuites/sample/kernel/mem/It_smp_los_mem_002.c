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

/* 用例简要描述: ItSmpLosMem002 双核 alloc 交叉跨核 free  */
#include "osTest.h"
#include "It_los_mem.h"
#include "los_memory.h"
#ifdef LOSCFG_KERNEL_SMP

#define SMP_MEM_ROUNDS 300
/* 双核 alloc 交叉跨核 free */
static VOID *g_ptrs[2][SMP_MEM_ROUNDS];
static volatile INT32 g_f02Ready = 0;
static volatile UINT32 g_err2 = 0;

static void MemF02(void)
{
    int i;
    for (i = 0; i < SMP_MEM_ROUNDS; i++) {
        g_ptrs[1][i] = LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, 96);
        if (g_ptrs[1][i] == NULL) { g_err2 = g_err2 + 1; }
    }
    LOS_AtomicInc((Atomic *)&g_f02Ready);
    while (LOS_AtomicRead((Atomic *)&g_f02Ready) < 2) { }
    for (i = 0; i < SMP_MEM_ROUNDS; i++) {
        if (g_ptrs[0][i] != NULL && LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, g_ptrs[0][i]) != LOS_OK) { g_err2 = g_err2 + 1; }
    }
    LOS_AtomicInc((Atomic *)&g_f02Ready);
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S t;
    UINT32 taskId;
    UINT32 used0, used1;
    int i;

    (void)memset_s(&t, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)MemF02;
    t.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    t.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    t.pcName = "smp_mem_002";
    t.usCpuAffiMask = CPUID_TO_AFFI_MASK((ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM);
    g_err2 = 0;
    ICUNIT_ASSERT_EQUAL(LOS_TaskCreate(&taskId, &t), LOS_OK, LOS_TaskCreate(&taskId, &t));

    for (i = 0; i < SMP_MEM_ROUNDS; i++) {
        g_ptrs[0][i] = LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, 96);
        if (g_ptrs[0][i] == NULL) { g_err2 = g_err2 + 1; }
    }
    LOS_AtomicInc((Atomic *)&g_f02Ready);
    while (LOS_AtomicRead((Atomic *)&g_f02Ready) < 2) { }
    for (i = 0; i < SMP_MEM_ROUNDS; i++) {
        if (g_ptrs[1][i] != NULL && LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, g_ptrs[1][i]) != LOS_OK) { g_err2 = g_err2 + 1; }
    }
    while (LOS_AtomicRead((Atomic *)&g_f02Ready) < 3) { }
    {   /* 帮手退出后取基线单核验证守恒 */
        UINT64 tEnd = TestTickCountGet() + 50;
        while (TestTickCountGet() < tEnd) { }
        used0 = LOS_MemTotalUsedGet((VOID *)OS_SYS_MEM_ADDR);
        for (i = 0; i < 100; i++) {
            VOID *p = LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, 96);
            if (p == NULL) { g_err2 = g_err2 + 1; continue; }
            if (LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, p) != LOS_OK) { g_err2 = g_err2 + 1; }
        }
        used1 = LOS_MemTotalUsedGet((VOID *)OS_SYS_MEM_ADDR);
    }
    ICUNIT_ASSERT_EQUAL(g_err2, 0, g_err2);
    ICUNIT_ASSERT_EQUAL(used1, used0, used1 - used0);
    return LOS_OK;
}
VOID ItSmpLosMem002(void)
{
    TEST_ADD_CASE("ItSmpLosMem002", Testcase, TEST_LOS, TEST_FN_KERNEL_DYNMEM, TEST_LEVEL1, TEST_FUNCTION);
}
#endif

