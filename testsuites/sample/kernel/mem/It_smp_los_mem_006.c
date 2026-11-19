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

/* 用例简要描述: ItSmpLosMem006 FreeByTaskID 跨核回收  */
#include "osTest.h"
#include "It_los_mem.h"
#include "los_memory.h"
#if defined(LOSCFG_KERNEL_SMP) && (LOSCFG_MEM_FREE_BY_TASKID == 1)

#define SMP_MEM_ROUNDS 300
/* FreeByTaskID 跨核回收 */
static volatile UINT32 g_006Leaked = 0;

static void MemF06(void)
{
    VOID *p = LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, 128);
    if (p == NULL) { g_006Leaked = g_006Leaked + 1; return; }
    g_006Leaked = g_006Leaked + 1;
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S t;
    UINT32 taskId;
    UINT32 used0, used1;

    (void)memset_s(&t, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)MemF06;
    t.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    t.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    t.pcName = "smp_mem_006";
    t.usCpuAffiMask = CPUID_TO_AFFI_MASK((ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM);
    ICUNIT_ASSERT_EQUAL(LOS_TaskCreate(&taskId, &t), LOS_OK, LOS_TaskCreate(&taskId, &t));
    {
        UINT64 tEnd = TestTickCountGet() + 50;
        while (g_006Leaked == 0 && TestTickCountGet() < tEnd) { }
    }
    used0 = LOS_MemTotalUsedGet((VOID *)OS_SYS_MEM_ADDR);
    (void)LOS_MemFreeByTaskID((VOID *)OS_SYS_MEM_ADDR, taskId);
    used1 = LOS_MemTotalUsedGet((VOID *)OS_SYS_MEM_ADDR);
    ICUNIT_ASSERT_EQUAL(g_006Leaked, 1, g_006Leaked);
    ICUNIT_ASSERT_EQUAL(used1 < used0, 1, used0 - used1);   /* 有回收（算法节点头大小不定，只判回收发生） */
    return LOS_OK;
}
VOID ItSmpLosMem006(void)
{
    TEST_ADD_CASE("ItSmpLosMem006", Testcase, TEST_LOS, TEST_FN_KERNEL_DYNMEM, TEST_LEVEL2, TEST_FUNCTION);
}
#endif

