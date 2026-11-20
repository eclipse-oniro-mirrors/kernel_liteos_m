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
 * MEM-TSK-006  Repeated create/exit with a drain each round  (group B)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * A box does not get unlimited tasks: its TCB pool is `LOSCFG_BOXA_TASK_LIMIT`
 * (= 6) slots in .box.res, and the entry task holds one of them. This case
 * exercises the pool boundary the way an application SHOULD: create a probe,
 * let it run and exit, DRAIN its user stack, then create the next one.
 *
 * The question it answers is: does the pool ever shrink? Each round must
 * consume exactly one user stack from the heap and return it, so after N rounds
 * the heap must be byte-identical to the baseline. A pool that lost a slot or a
 * stack that was never returned shows up immediately as a drift.
 *
 * ---------------------------------------------------------------------------
 * WHY IT DRAINS EVERY ROUND - PLEASE READ BEFORE "OPTIMISING" IT
 * ---------------------------------------------------------------------------
 * An earlier revision created several probes back-to-back and drained only at
 * the end. That PANICKED the board:
 *
 *     BoxID: 0x0, boxResRecycleStackNum is overflow:6
 *
 * and the panic is genuine - not a test artefact:
 *
 *   - OsTaskResRelease queues the user-stack ADDRESS of every deleted dynamic
 *     task onto boxResRecycleStackList;
 *   - that queue is sized `boxResTaskNum` (= TASK_LIMIT = 6), the SAME size as
 *     the TCB pool;
 *   - a TCB slot is reusable the moment the task is deleted, but a QUEUE slot is
 *     only freed when the application calls LOS_TaskStackRecycle;
 *   - so an application that creates and drops tasks faster than it drains can
 *     push more than 6 entries and hit
 *         if (boxResRecycleStackNum >= boxResTaskNum) LOS_Panic(...)
 *     from `los_task_box.c OsBoxUserStackRecycle`.
 *
 * In other words: a box that repeatedly spawns short-lived tasks without calling
 * LOS_TaskStackRecycle often enough can take down the whole system. That is a
 * design weakness worth reporting (and a DoS an unprivileged box can trigger),
 * but a REGRESSION case must not detonate it - hence one drain per round here.
 * The overflow itself is deliberately NOT probed by this suite; if you want to
 * reproduce it, do it on a crash-tolerant rig and expect a reset.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - "round N create err 0x..." with TSK_TCB_UNAVAILABLE  -> a TCB was leaked
 *     by an earlier round (the delete path did not recycle it);
 *   - the same with TSK_NO_MEMORY                          -> a user stack was
 *     not returned to the heap (drain or free misbehaved);
 *   - "heap drifted at round N"                            -> the round did not
 *     settle back to baseline: leak, or fragmentation that the settle loop
 *     could not clean up.
 *
 * Round count is BOX_MEM_TSK_ROUNDS (6): on this board each round costs a couple
 * of ticks and a tick is ~1s, so 6 rounds is ~15-20s. The assertions do not
 * depend on the count - raise it for a soak run.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

/* Case-local probe state (static: no link-time symbol, and two cases can never
 * share a flag by accident). */
static volatile UINT32 g_t6Ran;
static volatile UINT32 g_t6Alive;

static VOID MemTsk006Entry(VOID)
{
    g_t6Ran++;
    g_t6Alive--;
    /* return -> OsUsrTaskExit trampoline -> detached delete (kernel side) */
}

static UINT32 MemTsk006Case(VOID)
{
    static const CHAR *caseName = "MEM-TSK-006";
    BoxMemSnap s0;
    BoxMemSnap sNow;
    UINT32 taskId = 0;
    UINT32 ret;
    UINT32 fails = 0;
    UINT32 round;
    UINT32 firstDrift = 0xFFFFFFFFU;

    /* ---- 1) baseline ---- */
    if (!BoxMemSnapGet(&s0, "MEM-TSK-006 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-006: %u rounds, one probe each, drain per round; "
            "heap must stay at baseline\n", BOX_MEM_TSK_ROUNDS);

    g_t6Ran = 0;
    g_t6Alive = 0;

    /* ---- 2) create -> run -> exit -> DRAIN, repeated ----
     * The drain is what keeps the recycle queue shallow; see the header note. */
    for (round = 0; round < BOX_MEM_TSK_ROUNDS; round++) {
        UINT32 before = g_t6Ran;

        g_t6Alive = 1;
        ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk006Entry, "memTsk006",
                              BOX_MEM_TSK_STACK_SIZE, &taskId);
        if (ret != LOS_OK) {
            g_t6Alive = 0;
            /* In the drain-per-round pattern a refusal means a slot or a stack
             * was lost by an earlier round - exactly what this case hunts. */
            dprintf("[MEM] MEM-TSK-006 FAIL: round %u create err 0x%x "
                    "(TCB or user stack leaked by a previous round)\n", round, ret);
            fails++;
            break;
        }

        (VOID)BoxMemTskWaitFlag(&g_t6Ran, before + 1U);
        (VOID)BoxMemTskWaitFlag(&g_t6Alive, 0);

        /* Settle = tick + LOS_TaskStackRecycle until the pool matches baseline. */
        if (!BoxMemTskSettle(&s0, &sNow)) {
            if (firstDrift == 0xFFFFFFFFU) {
                firstDrift = round;
            }
            fails += BoxMemFail(caseName, "round did not settle to baseline",
                                sNow.freeSize, s0.freeSize);
        }
        dprintf("[MEM] MEM-TSK-006 round %u: free=%u freeNodes=%u maxFree=%u%s\n",
                round, sNow.freeSize, sNow.freeNodes, sNow.maxFree,
                BoxMemSame(&s0, &sNow) ? "" : "  <== DRIFT");
    }

    /* ---- 3) report the trend ---- */
    if (firstDrift == 0xFFFFFFFFU) {
        dprintf("[MEM] MEM-TSK-006: %u create/exit/drain rounds stable - "
                "pool neither shrank nor drifted\n", BOX_MEM_TSK_ROUNDS);
    } else {
        dprintf("[MEM] MEM-TSK-006: first drift at round %u\n", firstDrift);
    }

    /* ---- 4) one more create must still succeed: the pool is intact ---- */
    g_t6Alive = 1;
    ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk006Entry, "memTsk006z",
                          BOX_MEM_TSK_STACK_SIZE, &taskId);
    if (ret != LOS_OK) {
        g_t6Alive = 0;
        dprintf("[MEM] MEM-TSK-006 FAIL: final create err 0x%x "
                "(pool lost a slot)\n", ret);
        fails++;
    } else {
        dprintf("[MEM] MEM-TSK-006: final create OK (taskId=0x%x)\n", taskId);
        (VOID)BoxMemTskWaitFlag(&g_t6Alive, 0);
        (VOID)BoxMemTskSettle(&s0, NULL);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemTsk006(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemTsk006", MemTsk006Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
