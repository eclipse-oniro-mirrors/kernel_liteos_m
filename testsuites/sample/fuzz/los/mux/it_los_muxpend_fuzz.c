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

/* ItFuzzLosMuxPend: LOS_MuxPend */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_los_mux_fuzz.h"

static VOID __attribute__((unused)) FzTaskEntry(UINT32 arg)
{
    for (;;) { LOS_TaskDelay(10 + (arg & 0xF)); }
}

static VOID __attribute__((unused)) FzTaskJoinEntry(UINT32 arg)
{
    LOS_TaskDelay(1 + (arg & 0x3));
    (VOID)LOS_TaskDelete(LOS_CurTaskIDGet());
}

static UINT32 g_fzMuxH;
static volatile UINT32 g_fzMuxHeld;
static VOID FzMuxHolderEntry(UINT32 arg)
{
    (VOID)arg;
    if (LOS_MuxPend(g_fzMuxH, 0) == LOS_OK) {
        g_fzMuxHeld = 1;
        LOS_TaskDelay(8);
        (VOID)LOS_MuxPost(g_fzMuxH);
    }
    (VOID)LOS_TaskDelete(LOS_CurTaskIDGet());
}

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    UINT32 h = 0;
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        if (LOS_MuxCreate(&h) != LOS_OK) continue;
        (VOID)LOS_MuxPend(h, 0);   /* timeout=0: non-blocking (see directed segment for blocking paths) */
        (VOID)LOS_MuxPost(h);
        (VOID)LOS_MuxDelete(h);
    }

    /* directed: blocked pend + priority-inheritance paths */
    {
        UINT32 m2 = 0;
        if (LOS_MuxCreate(&m2) == LOS_OK) {
            g_fzMuxH = m2; g_fzMuxHeld = 0;
            UINT32 th = 0; UINT32 w = 0;
            TSK_INIT_PARAM_S ph = {0};
            ph.pcName = "FzMxH"; ph.pfnTaskEntry = (TSK_ENTRY_FUNC)FzMuxHolderEntry;
            ph.uwStackSize = 0x400; ph.usTaskPrio = 27;   /* lower prio than self */
            if (LOS_TaskCreate(&th, &ph) == LOS_OK) {
                while (g_fzMuxHeld == 0 && w < 100) { LOS_TaskDelay(1); w++; }
                (VOID)LOS_MuxPend(m2, 6);   /* blocked on holder: wait/bitmap paths */
                LOS_TaskDelay(2);
                (VOID)LOS_TaskDelete(th);
            }
            (VOID)LOS_MuxDelete(m2);
        }
        (VOID)LOS_MuxPend(LOSCFG_BASE_IPC_MUX_LIMIT + 1, 0);
        (VOID)LOS_MuxPost(LOSCFG_BASE_IPC_MUX_LIMIT + 1);
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzLosMuxPend(VOID)
{
    TEST_ADD_CASE("ItFuzzLosMuxPend", TestCase, TEST_LOS, TEST_MUX, TEST_LEVEL0, TEST_FUNCTION);
}
