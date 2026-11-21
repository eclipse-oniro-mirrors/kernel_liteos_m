/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the following conditions:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ItLosBoxUsrTick002 - usr-side DIRECT-CALL tick face (no syscalls): the
 * refs USRSPACE trio reaching boxA through plain COMMON_TEXT copies.
 *
 *   LOS_MS2Tick / LOS_Tick2MS - refs conversion pair, ceil + UINT32_MAX
 *     sentinel (the OHOS vanilla pair floors and uses OS_NULL_INT - each
 *     world stays faithful to its own source, recorded in los_usr_api.c)
 *   LOS_TickCountGet - a plain read of the R2 user-readable tick counter
 *     (g_usrTickCount; refs publishes its g_tickCount the same way)
 *
 *  phase 1 - conversions: sentinel pass-through, tick<->ms round trip, and
 *     the identity mapping (this config: TICK_PER_SECOND == MS_PER_SECOND,
 *     so every value converts to itself)
 *  phase 2 - TickCountGet monotonic across a 3-tick delay, delta bounded
 *     (board quirk: one tick measures ~1s, see uts_phase.h) */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

static UINT32 TestCase(VOID)
{
    UINT64 tick1;
    UINT64 tick2;
    UINT32 ms;
    UINT32 fails = 0;
    UINT32 ret;

    /* ---- phase 1: conversion contract ---- */
    /* sentinel pass-through */
    ret = LOS_MS2Tick(UINT32_MAX);
    if (ret != UINT32_MAX) {
        fails++;
        LOS_PrintStr("usr ms2tick sentinel FAIL\n");
    }

    /* identity + round trip over a spread of values (1000 ticks/s and
     * 1000 ms/s: every finite value maps to itself in both directions) */
    {
        const UINT32 probes[] = {1U, 2U, 3U, 10U, 100U, 1000U, 65000U};
        const UINT32 n = sizeof(probes) / sizeof(probes[0]);
        UINT32 i;

        for (i = 0; i < n; i++) {
            ms = LOS_Tick2MS(LOS_MS2Tick(probes[i]));
            if (ms != probes[i]) {
                fails++;
                LOS_PrintStr("usr ms2tick roundtrip FAIL\n");
                break;
            }
        }
    }

    /* ---- phase 2: TickCountGet monotonic across a delay ---- */
    tick1 = LOS_TickCountGet();
    LOS_TaskDelay(3);
    tick2 = LOS_TickCountGet();
    if ((tick2 <= tick1) || ((tick2 - tick1) > 10)) {
        fails++;
        dprintf("usr tickcnt t1 0x%x%x t2 0x%x%x\n",
                (UINT32)(tick1 >> 32), (UINT32)tick1, (UINT32)(tick2 >> 32), (UINT32)tick2);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrTick002(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrTick002", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
