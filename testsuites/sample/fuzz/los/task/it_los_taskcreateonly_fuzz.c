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

/* ItFuzzLosTaskCreateOnly: LOS_TaskCreateOnly */
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

    UINT32 tid = 0; UINT32 ret;
    TSK_INIT_PARAM_S param = {0};
    param.pcName = "FzTC"; param.pfnTaskEntry = (TSK_ENTRY_FUNC)FzTaskEntry;
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        param.usTaskPrio = (*(UINT32 *)DT_SetGetU32(&g_element[0], 20)) % 32;
        ret = LOS_TaskCreateOnly(&tid, &param);
        if (ret == LOS_OK) { LOS_TaskDelete(tid); }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzLosTaskCreateOnly(VOID)
{
    TEST_ADD_CASE("ItFuzzLosTaskCreateOnly", TestCase, TEST_LOS, TEST_TASK, TEST_LEVEL0, TEST_FUNCTION);
}
