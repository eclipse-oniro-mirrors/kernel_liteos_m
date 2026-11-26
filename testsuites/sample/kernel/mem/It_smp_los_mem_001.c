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

/* 用例简要描述: ItSmpLosMem001 双核并发 alloc/free 守恒（全局锁串行正确性） */
#include "osTest.h"
#include "It_los_mem.h"
#include "los_memory.h"
#ifdef LOSCFG_KERNEL_SMP

#define SMP_MEM_ROUNDS 300

static volatile INT32 g_done1 = 0;
static volatile UINT32 g_err1 = 0;

static void MemF01(void)
{
    int i;
    VOID *p;
    for (i = 0; i < SMP_MEM_ROUNDS; i++) {
        p = LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, 64);
        if (p == NULL) { g_err1 = g_err1 + 1; continue; }
        if (LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, p) != LOS_OK) { g_err1 = g_err1 + 1; }
    }
    LOS_AtomicInc((Atomic *)&g_done1);
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S t;
    UINT32 taskId;
    UINT32 used0, used1;
    int i;

    (void)memset_s(&t, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)MemF01;
    t.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    t.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    t.pcName = "smp_mem_001";
    t.usCpuAffiMask = CPUID_TO_AFFI_MASK((ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM);
    g_done1 = 0;
    g_err1 = 0;
    ICUNIT_ASSERT_EQUAL(LOS_TaskCreate(&taskId, &t), LOS_OK, LOS_TaskCreate(&taskId, &t));

    for (i = 0; i < SMP_MEM_ROUNDS; i++) {
        VOID *p = LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, 64);
        if (p == NULL) { g_err1 = g_err1 + 1; continue; }
        if (LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, p) != LOS_OK) { g_err1 = g_err1 + 1; }
    }
    {
        UINT64 tEnd = TestTickCountGet() + 300;
        while (g_done1 == 0 && TestTickCountGet() < tEnd) { }
    }
    {   /* 帮手退出+栈回收完成后再取守恒基线（帮手退出贡献会破坏 used 差值） */
        UINT64 tEnd = TestTickCountGet() + 50;
        while (TestTickCountGet() < tEnd) { }
        used0 = LOS_MemTotalUsedGet((VOID *)OS_SYS_MEM_ADDR);
        for (i = 0; i < 100; i++) {
            VOID *p = LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, 64);
            if (p == NULL) { g_err1 = g_err1 + 1; continue; }
            if (LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, p) != LOS_OK) { g_err1 = g_err1 + 1; }
        }
        used1 = LOS_MemTotalUsedGet((VOID *)OS_SYS_MEM_ADDR);
    }
    ICUNIT_ASSERT_EQUAL(g_done1, 1, g_done1);
    ICUNIT_ASSERT_EQUAL(g_err1, 0, g_err1);
    ICUNIT_ASSERT_EQUAL(used1, used0, used1 - used0);
    ICUNIT_ASSERT_EQUAL(LOS_MemIntegrityCheck((VOID *)OS_SYS_MEM_ADDR), LOS_OK, LOS_MemIntegrityCheck((VOID *)OS_SYS_MEM_ADDR));
    return LOS_OK;
}

VOID ItSmpLosMem001(void)
{
    TEST_ADD_CASE("ItSmpLosMem001", Testcase, TEST_LOS, TEST_FN_KERNEL_DYNMEM, TEST_LEVEL1, TEST_FUNCTION);
}
#endif
