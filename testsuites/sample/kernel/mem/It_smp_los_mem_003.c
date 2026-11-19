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

/* 用例简要描述: ItSmpLosMem003 membox 双核并发（独立 g_memboxSpin）  */
#include "osTest.h"
#include "It_los_mem.h"
#include "los_memory.h"
#include "los_membox.h"
#ifdef LOSCFG_KERNEL_SMP

#define SMP_MEM_ROUNDS 300
/* membox 双核并发（独立 g_memboxSpin） */
#define SMP_BOX_BLKS 64
#define SMP_BOX_BLK_SIZE 32
static UINT8 g_smpBoxPool[SMP_BOX_BLKS * SMP_BOX_BLK_SIZE] __attribute__((aligned(8)));
static volatile UINT32 g_err3 = 0;
static volatile INT32 g_done3 = 0;

static void MemF03(void)
{
    int i;
    VOID *p;
    for (i = 0; i < 200; i++) {
        p = LOS_MemboxAlloc(g_smpBoxPool);
        if (p == NULL) { g_err3 = g_err3 + 1; continue; }
        if (LOS_MemboxFree(g_smpBoxPool, p) != LOS_OK) { g_err3 = g_err3 + 1; }
    }
    LOS_AtomicInc((Atomic *)&g_done3);
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S t;
    UINT32 taskId;
    int i;
    UINT32 ret = LOS_MemboxInit(g_smpBoxPool, sizeof(g_smpBoxPool), SMP_BOX_BLK_SIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    (void)memset_s(&t, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    t.pfnTaskEntry = (TSK_ENTRY_FUNC)MemF03;
    t.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    t.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    t.pcName = "smp_mem_003";
    t.usCpuAffiMask = CPUID_TO_AFFI_MASK((ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM);
    g_err3 = 0;
    g_done3 = 0;
    ICUNIT_ASSERT_EQUAL(LOS_TaskCreate(&taskId, &t), LOS_OK, LOS_TaskCreate(&taskId, &t));

    for (i = 0; i < 200; i++) {
        VOID *p = LOS_MemboxAlloc(g_smpBoxPool);
        if (p == NULL) { g_err3 = g_err3 + 1; continue; }
        if (LOS_MemboxFree(g_smpBoxPool, p) != LOS_OK) { g_err3 = g_err3 + 1; }
    }
    {
        UINT64 tEnd = TestTickCountGet() + 200;
        while (g_done3 == 0 && TestTickCountGet() < tEnd) { }
    }
    ICUNIT_ASSERT_EQUAL(g_done3, 1, g_done3);
    ICUNIT_ASSERT_EQUAL(g_err3, 0, g_err3);
    return LOS_OK;
}
VOID ItSmpLosMem003(void)
{
    TEST_ADD_CASE("ItSmpLosMem003", Testcase, TEST_LOS, TEST_FN_KERNEL_DYNMEM, TEST_LEVEL1, TEST_FUNCTION);
}
#endif

