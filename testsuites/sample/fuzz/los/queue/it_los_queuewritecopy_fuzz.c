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

/* ItFuzzLosQueueWrite: LOS_QueueWriteCopy */
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

    UINT32 qid = 0; CHAR buf[64] = {0};
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        if (LOS_QueueCreate("FzQ", 4, &qid, 0, 8) != LOS_OK) continue;
        (VOID)LOS_QueueWriteCopy(qid, buf, ((*(UINT32 *)DT_SetGetU32(&g_element[0], 8)) & 0x3F) + 1, 1);
        (VOID)LOS_QueueDelete(qid);
    }

    /* directed: non-copy write/read + write-head/read flavors */
    {
        UINT32 q2 = 0; CHAR b2[8] = {0}; UINT32 sz2 = sizeof(b2);
        if (LOS_QueueCreate("FzQ2", 2, &q2, 0, 8) == LOS_OK) {
            (VOID)LOS_QueueWrite(q2, b2, 8, 0);
            (VOID)LOS_QueueWriteHead(q2, b2, 8, 0);
            (VOID)LOS_QueueRead(q2, b2, sz2, 0);
            (VOID)LOS_QueueReadCopy(q2, b2, &sz2, 0);
            (VOID)LOS_QueueDelete(q2);
        }
    }
    /* directed: static-allocation queue create/write/read/delete */
#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    {
        static CHAR qmem[256];
        UINT32 q3 = 0;
        if (LOS_QueueCreateStatic("FzQS", 2, &q3, 0, 8, qmem, sizeof(qmem)) == LOS_OK) {
            CHAR b3[8] = {0}; UINT32 sz3 = sizeof(b3);
            (VOID)LOS_QueueWrite(q3, b3, 8, 0);
            (VOID)LOS_QueueRead(q3, b3, sz3, 0);
            (VOID)LOS_QueueDelete(q3);
        }
    }
#endif
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzLosQueueWrite(VOID)
{
    TEST_ADD_CASE("ItFuzzLosQueueWrite", TestCase, TEST_LOS, TEST_QUE, TEST_LEVEL0, TEST_FUNCTION);
}
