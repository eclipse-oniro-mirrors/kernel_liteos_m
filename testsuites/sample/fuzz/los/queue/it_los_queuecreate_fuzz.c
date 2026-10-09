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

/* ItFuzzLosQueueCreate: LOS_QueueCreate */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_los_queue_fuzz.h"

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

    UINT32 qid = 0; UINT32 ret;
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        ret = LOS_QueueCreate("FzQ",
            (UINT16)((*(UINT32 *)DT_SetGetU32(&g_element[0], 4)) % 32 + 1),
            &qid, 0, (UINT16)((*(UINT32 *)DT_SetGetU32(&g_element[1], 8)) % 64 + 1));
        if (ret == LOS_OK) { (VOID)LOS_QueueDelete(qid); }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzLosQueueCreate(VOID)
{
    TEST_ADD_CASE("ItFuzzLosQueueCreate", TestCase, TEST_LOS, TEST_QUE, TEST_LEVEL0, TEST_FUNCTION);
}
