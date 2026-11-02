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

/* ItFuzzLosAtomic: LOS_Atomic* family */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_los_misc_fuzz.h"

static VOID __attribute__((unused)) FzHwiHandler(VOID *parm) { (VOID)parm; }
static VOID __attribute__((unused)) FzTaskEntry(UINT32 arg)
{
    for (;;) { LOS_TaskDelay(10 + (arg & 0xF)); }
}

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    Atomic atom = {0}; Atomic64 atom64 = {0};
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        LOS_AtomicSet(&atom, *(INT32 *)DT_SetGetS32(&g_element[0], 0));
        (VOID)LOS_AtomicRead(&atom);
        (VOID)LOS_AtomicXchg32bits(&atom, *(INT32 *)DT_SetGetS32(&g_element[1], 0));
        (VOID)LOS_AtomicCmpXchg32bits(&atom, *(INT32 *)DT_SetGetS32(&g_element[2], 0),
                                     *(INT32 *)DT_SetGetS32(&g_element[3], 0));
        LOS_Atomic64Set(&atom64, DT_SetGetS64(&g_element[4], 0));
        (VOID)LOS_Atomic64Read(&atom64);
    }

    /* directed: inc/dec/add/sub + 64-bit family */
    for (int i = 0; i < 4; i++) {
        LOS_AtomicInc(&atom); LOS_AtomicDec(&atom);
        (VOID)LOS_AtomicIncRet(&atom); (VOID)LOS_AtomicDecRet(&atom);
        (VOID)LOS_AtomicAdd(&atom, 2); (VOID)LOS_AtomicSub(&atom, 1);
        LOS_Atomic64Inc(&atom64); LOS_Atomic64Dec(&atom64);
        (VOID)LOS_Atomic64IncRet(&atom64); (VOID)LOS_Atomic64DecRet(&atom64);
        (VOID)LOS_Atomic64Add(&atom64, 2); (VOID)LOS_Atomic64Sub(&atom64, 1);
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzLosAtomic(VOID)
{
    TEST_ADD_CASE("ItFuzzLosAtomic", TestCase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}
