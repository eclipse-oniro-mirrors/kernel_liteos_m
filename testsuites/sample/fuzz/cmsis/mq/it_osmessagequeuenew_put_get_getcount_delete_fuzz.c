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

/* ItFuzzOsMqLifecycle: osMessageQueueNew/Put/Get/GetCount/Delete */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_cmsis_mq_fuzz.h"

static void __attribute__((unused)) FzCmsisThreadEntry(void *arg) { (VOID)arg; osDelay(1); }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    osMessageQueueId_t mid; UINT8 msg[8] = {0};
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        mid = osMessageQueueNew((*(UINT32 *)DT_SetGetU32(&g_element[0], 4)) % 32 + 1,
                                (*(UINT32 *)DT_SetGetU32(&g_element[1], 8)) % 64 + 1, NULL);
        if (mid != NULL) {
            (VOID)osMessageQueuePut(mid, msg, 0, 1);
            (VOID)osMessageQueueGet(mid, msg, NULL, 1);
            (VOID)osMessageQueueGetCount(mid);             (VOID)osMessageQueueGetName(mid);
            (VOID)osMessageQueueGetCapacity(mid);
            (VOID)osMessageQueueGetMsgSize(mid);
            (VOID)osMessageQueueGetSpace(mid);
(VOID)osMessageQueueDelete(mid);
        }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzOsMqLifecycle(VOID)
{
    TEST_ADD_CASE("ItFuzzOsMqLifecycle", TestCase, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
}
