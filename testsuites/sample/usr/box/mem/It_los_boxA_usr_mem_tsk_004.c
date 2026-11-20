/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
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

/* ============================================================================
 * MEM-TSK-004  Long create/exit/recycle loop must not drift   (group B)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * MEM-TSK-001 measures ONE task. MEM-TSK-002 explains the two-stage recycle.
 * This case is the endurance counterpart: the CORRECT usage pattern, repeated,
 * must be perfectly stable - no heap drift, and (just as important) no
 * fragmentation creep.
 *
 * The correct pattern per round, exactly as a real box application should do it:
 *
 *     create probe -> let it run and exit -> LOS_TaskStackRecycle()
 *
 * Two independent kinds of drift are watched, because they fail differently:
 *
 *   freeSize   -> total bytes withheld. Growth here is a real leak.
 *   freeNodes  -> number of free fragments. Growth here is FRAGMENTATION: the
 *                 same total may be free but scattered into more pieces, which
 *                 eventually defeats large allocations even though `freeSize`
 *                 looks healthy.
 *
 * maxFree is watched for the same reason: after every round the pool must have
 * merged back into one big block. If `freeSize` returns to baseline but
 * `maxFree` does not, the merge logic is losing adjacency - a fragmentation bug
 * that a freeSize-only check would miss entirely.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * Read the per-round lines; the interesting ones are the last of each pair:
 *
 *   [MEM] MEM-TSK-004 round k: free=... freeNodes=... maxFree=...
 *
 * All rounds must report the SAME numbers as the baseline. Any monotonic trend
 * is a defect even if no single round "fails" yet - that is why the case also
 * tracks the first round whose snapshot differs from baseline and prints it.
 *
 * Round count is BOX_MEM_TSK_ROUNDS (6) by default: each round costs a couple
 * of ticks and this board ticks at ~1s, so 6 rounds is ~15-20s. Raise it for a
 * soak run; the assertions do not depend on the count.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

static volatile UINT32 g_t4Ran;
static volatile UINT32 g_t4Alive;

static VOID MemTsk004Entry(VOID)
{
    g_t4Ran++;
    g_t4Alive--;
}

static UINT32 MemTsk004Case(VOID)
{
    static const CHAR *caseName = "MEM-TSK-004";
    BoxMemSnap s0;
    BoxMemSnap sNow;
    UINT32 taskId = 0;
    UINT32 ret;
    UINT32 fails = 0;
    UINT32 round;
    UINT32 firstDrift = 0xFFFFFFFFU;

    /* ---- 1) baseline ---- */
    if (!BoxMemSnapGet(&s0, "MEM-TSK-004 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-004: %u rounds of create -> exit -> recycle; "
            "free/freeNodes/maxFree must all stay at baseline\n",
            BOX_MEM_TSK_ROUNDS);

    g_t4Ran = 0;
    g_t4Alive = 0;

    /* ---- 2) the correct pattern, repeated ---- */
    for (round = 0; round < BOX_MEM_TSK_ROUNDS; round++) {
        UINT32 before = g_t4Ran;

        g_t4Alive = 1;
        ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk004Entry, "memTsk004",
                              BOX_MEM_TSK_STACK_SIZE, &taskId);
        if (ret != LOS_OK) {
            g_t4Alive = 0;
            /* In the CORRECT pattern this must never happen: each round gives
             * its resources back before the next one asks. A refusal therefore
             * means the recycle path leaked, which is precisely the bug this
             * case hunts. */
            dprintf("[MEM] MEM-TSK-004 FAIL: round %u create err 0x%x "
                    "(leak in the recycle path?)\n", round, ret);
            fails++;
            break;
        }

        /* Let it run and exit, then DRAIN - this is the correct usage. */
        (VOID)BoxMemTskWaitFlag(&g_t4Ran, before + 1U);
        (VOID)BoxMemTskWaitFlag(&g_t4Alive, 0);

        if (!BoxMemTskSettle(&s0, &sNow)) {
            if (firstDrift == 0xFFFFFFFFU) {
                firstDrift = round;
            }
            fails += BoxMemFail(caseName, "round did not settle to baseline",
                                sNow.freeSize, s0.freeSize);
        }
        dprintf("[MEM] MEM-TSK-004 round %u: free=%u freeNodes=%u maxFree=%u%s\n",
                round, sNow.freeSize, sNow.freeNodes, sNow.maxFree,
                BoxMemSame(&s0, &sNow) ? "" : "  <== DRIFT");

        if (!BoxMemSame(&s0, &sNow)) {
            if (firstDrift == 0xFFFFFFFFU) {
                firstDrift = round;
            }
            /* Keep going: seeing whether the drift ACCUMULATES or is a one-off
             * is the most useful diagnostic, so the loop does not bail out. */
        }
    }

    /* ---- 3) report the trend ---- */
    if (firstDrift != 0xFFFFFFFFU) {
        dprintf("[MEM] MEM-TSK-004: first drift at round %u\n", firstDrift);
    } else {
        dprintf("[MEM] MEM-TSK-004: no drift across %u rounds "
                "(free, freeNodes and maxFree all stable - no leak, no "
                "fragmentation creep)\n", BOX_MEM_TSK_ROUNDS);
    }

    /* ---- 4) one last snapshot for the record ---- */
    if (BoxMemSnapGet(&sNow, "MEM-TSK-004 final")) {
        if (!BoxMemSame(&s0, &sNow)) {
            fails += BoxMemFail(caseName, "final snapshot != baseline",
                                sNow.freeSize, s0.freeSize);
        }
    } else {
        fails++;
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemTsk004(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemTsk004", MemTsk004Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
