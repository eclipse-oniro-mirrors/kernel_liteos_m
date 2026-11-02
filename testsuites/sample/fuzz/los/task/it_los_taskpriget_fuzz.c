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

/* ItFuzzLosTaskPriGet: LOS_TaskPriGet */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_los_task_fuzz.h"

static VOID __attribute__((unused)) FzTaskEntry(UINT32 arg)
{
    for (;;) { LOS_TaskDelay(10 + (arg & 0xF)); }
}

static VOID __attribute__((unused)) FzTaskJoinEntry(UINT32 arg)
{
    LOS_TaskDelay(1 + (arg & 0x3));
    (VOID)LOS_TaskDelete(LOS_CurTaskIDGet());
}

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    UINT32 tid = 0;
    UINT32 ret;
    TSK_INIT_PARAM_S param = {0};
    param.pcName = "FzT";
    param.pfnTaskEntry = (TSK_ENTRY_FUNC)FzTaskEntry;
    param.uwStackSize = 0x400;
    param.usTaskPrio = 20;
    INIT_FuzzEnvironment();
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        ret = LOS_TaskCreate(&tid, &param); if (ret != LOS_OK) continue;
        /* dynamic resource guard: mutations hitting pre-existing resources
         * (snapshot at INIT_FuzzEnvironment) are redirected to our own tid */
        UINT32 mId = FuzzGuardTaskId(*(UINT32 *)DT_SetGetU32(&g_element[0], tid), tid);
(VOID)LOS_TaskPriGet(mId);
        LOS_TaskDelete(tid);
    }

    /* directed: task info/status/name/curpri/isscheduled/msleep */
    {
        UINT32 t2 = 0;
        TSK_INIT_PARAM_S p2 = {0};
        p2.pcName = "FzTI"; p2.pfnTaskEntry = (TSK_ENTRY_FUNC)FzTaskEntry;
        p2.uwStackSize = 0x400; p2.usTaskPrio = 27;
        if (LOS_TaskCreate(&t2, &p2) == LOS_OK) {
            TSK_INFO_S info = {0}; UINT32 st = 0;
            (VOID)LOS_TaskInfoGet(t2, &info);
            (VOID)LOS_TaskStatusGet(t2, &st);
            (VOID)LOS_TaskNameGet(t2);
            (VOID)LOS_TaskDelete(t2);
        }
        (VOID)LOS_CurTaskPriSet(28);
        (VOID)LOS_TaskIsScheduled();
    }
    (VOID)LOS_Msleep(1);
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzLosTaskPriGet(VOID)
{
    TEST_ADD_CASE("ItFuzzLosTaskPriGet", TestCase, TEST_LOS, TEST_TASK, TEST_LEVEL0, TEST_FUNCTION);
}
