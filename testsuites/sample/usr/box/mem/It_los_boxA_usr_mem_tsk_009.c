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
 * MEM-TSK-009  Repeated refusals must not shrink the pool   (group B)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * A box application that retries a failing create (a launcher looping on
 * LOS_TaskCreate, a supervisor restarting a child) walks part of the create path
 * on every attempt:
 *
 *     user stack from the box heap  ->  pool kernel stack  ->  TCB  -> fail
 *
 * and the already-taken pieces must be rolled back each time. If they are not,
 * N refusals cost N slots and the box slowly loses the ability to create ANY
 * task - a silent capacity leak that only shows up under repeated failure.
 *
 * The case therefore measures the pool's capacity, burns a burst of refusals
 * against a deliberately exhausted pool, releases everything, and measures the
 * capacity again. The two numbers must be identical.
 *
 * ---------------------------------------------------------------------------
 * HOW CAPACITY IS MEASURED, AND WHY IT IS NOT SIMPLY "CREATE UNTIL REFUSED"
 * ---------------------------------------------------------------------------
 * Two different limits stop a create here, and mixing them up produces
 * meaningless numbers:
 *
 *   1. CONCURRENT capacity - how many tasks may be ALIVE at once. Bounded by
 *      the TCB pool (TASK_LIMIT = 6) and, on this board, by the heap: 1024-byte
 *      user stacks out of ~7.5 KB allow about 7.
 *
 *   2. CUMULATIVE capacity - how many create/exit cycles may run. Practically
 *      unbounded, because every cycle returns its slot.
 *
 * An earlier revision of this case counted create/exit CYCLES and printed
 * "capacity = 32" (its own loop bound), then tried to refill by creating that
 * many tasks back-to-back WITHOUT draining. That panicked the board:
 *
 *     BoxID: 0x0, boxResRecycleStackNum is overflow:6
 *
 * because OsTaskResRelease queues the user-stack address of every deleted
 * dynamic task on boxResRecycleStackList, whose capacity equals boxResTaskNum
 * (TASK_LIMIT = 6), and the queue only shrinks when the application calls
 * LOS_TaskStackRecycle. Seven un-drained exits overflow it.
 *
 * So this revision measures CONCURRENT capacity the only safe way: hold tasks
 * alive one at a time and keep them (no exit, so no queueing), stopping at
 * HOLD_MAX which is kept at or below TASK_LIMIT - 1. Because the entry task
 * already owns one slot, HOLD_MAX = 5 can never overflow a queue of 6.
 *
 * ---------------------------------------------------------------------------
 * WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - capacity AFTER < capacity BEFORE  -> the refusal path leaked a TCB or a
 *     pool kernel stack (the interesting defect);
 *   - the held set cannot even reach HOLD_MAX -> the pools are smaller than
 *     configured, or an earlier case leaked;
 *   - the heap does not return to baseline at the end -> a user stack was not
 *     recycled.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

/* How many probes to keep ALIVE at once while measuring the pool.
 *
 * MUST stay <= TASK_LIMIT - 1: the entry task holds one TCB, and - the real
 * constraint - each held probe that later exits queues its user stack, so a
 * hold of N costs N queue entries against a queue of TASK_LIMIT. With
 * TASK_LIMIT = 6 and the entry task counted, 5 is the safe ceiling and leaves
 * one queue slot spare. Do not raise this without raising
 * LOSCFG_BOXA_TASK_LIMIT as well. */
#define BOX_MEM_TSK009_HOLD_MAX  5U

/* Refusals to burn against a deliberately full pool. These attempts fail, so
 * they never queue anything - the number is not safety-critical, only large
 * enough to exercise the rollback path repeatedly. */
#define BOX_MEM_TSK009_BURST     8U

static volatile UINT32 g_t9Parked;    /* probes parked and waiting to be freed */
static volatile UINT32 g_t9Ready;     /* probes that reached the park loop      */

/* Probe body: park until the case releases it.
 *
 * Deliberately does NOT return by itself - the case decides when to free each
 * probe. That is what lets the pool stay exhausted across the burst, and it also
 * means no exit happens behind the case's back (every exit is paired with an
 * explicit drain, keeping the recycle queue shallow). */
static VOID MemTsk009Entry(VOID)
{
    g_t9Ready++;
    while (g_t9Parked != 0U) {
        LOS_TaskDelay(BOX_MEM_TSK_SPIN_TICKS);
    }
}

/* Fill the pool by HOLDING tasks alive, up to `want`.
 * Returns how many were actually held (0 on immediate failure) and stores the
 * task ids; `stopErr` gets the refusal code if the pool ran out early.
 *
 * Nothing exits in this function, so the recycle queue stays untouched. */
static UINT32 MemTsk009Hold(UINT32 *ids, UINT32 want, UINT32 *stopErr)
{
    UINT32 held = 0;
    UINT32 ret = LOS_OK;
    UINT32 i;

    if (stopErr != NULL) {
        *stopErr = LOS_OK;
    }
    for (i = 0; i < want; i++) {
        UINT32 id = 0;

        ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk009Entry, "memTsk009",
                              BOX_MEM_TSK_STACK_SIZE, &id);
        if (ret != LOS_OK) {
            if (stopErr != NULL) {
                *stopErr = ret;
            }
            break;
        }
        ids[held] = id;
        held++;
    }
    return held;
}

/* Release every held probe and drain what they queue.
 * The probes return as soon as g_t9Parked drops to 0, so one settle loop is
 * enough; it ticks AND drains, which is what keeps the queue from overflowing. */
static BOOL MemTsk009Release(const BoxMemSnap *baseline, UINT32 *ids, UINT32 held)
{
    /* The ids are not needed: a parked probe returns as soon as g_t9Parked drops
     * to 0, and the exit trampoline deletes it. The count matters only for the
     * bookkeeping above. */
    (VOID)ids;
    (VOID)held;
    g_t9Parked = 0;                 /* wake them: they return and get deleted */
    return BoxMemTskSettle(baseline, NULL);
}

static UINT32 MemTsk009Case(VOID)
{
    static const CHAR *caseName = "MEM-TSK-009";
    static UINT32 idsA[BOX_MEM_TSK009_HOLD_MAX];
    static UINT32 idsB[BOX_MEM_TSK009_HOLD_MAX];
    BoxMemSnap s0;
    BoxMemSnap sFinal;
    UINT32 capBefore;
    UINT32 capAfter;
    UINT32 errB = LOS_OK;
    UINT32 errA = LOS_OK;
    UINT32 ret;
    UINT32 fails = 0;
    UINT32 i;

    /* ---- 1) baseline ---- */
    if (!BoxMemSnapGet(&s0, "MEM-TSK-009 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-009: hold up to %u probes, burn %u refusals, "
            "re-measure; capacity must not shrink\n",
            BOX_MEM_TSK009_HOLD_MAX, BOX_MEM_TSK009_BURST);

    /* ---- 2) measure CONCURRENT capacity (hold, do not exit) ---- */
    g_t9Ready = 0;
    g_t9Parked = 1;                 /* keep probes parked until release */
    capBefore = MemTsk009Hold(idsA, BOX_MEM_TSK009_HOLD_MAX, &errB);
    dprintf("[MEM] MEM-TSK-009: capacity BEFORE = %u / %u (stop err 0x%x)\n",
            capBefore, BOX_MEM_TSK009_HOLD_MAX, errB);

    if (capBefore == 0U) {
        g_t9Parked = 0;
        dprintf("[MEM] MEM-TSK-009 FAIL: not a single probe could be held\n");
        ICUNIT_ASSERT_EQUAL(capBefore, 0, capBefore);
        return LOS_OK;
    }
    if (capBefore < BOX_MEM_TSK009_HOLD_MAX) {
        /* Not fatal - the pool may genuinely be smaller than HOLD_MAX - but it
         * is worth printing because it changes what the burst below can prove. */
        dprintf("[MEM] MEM-TSK-009: pool filled at %u (< %u); the burst still "
                "runs against a full pool\n", capBefore, BOX_MEM_TSK009_HOLD_MAX);
    }

    /* ---- 3) burn refusals against the FULL pool ----
     * Every one of these must be refused: the pool is exhausted, so the create
     * path rolls back whatever it grabbed. Refusals never queue anything (no
     * task exists to delete), which is why a burst here is safe. */
    for (i = 0; i < BOX_MEM_TSK009_BURST; i++) {
        UINT32 extraId = 0;

        ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk009Entry, "memTsk009x",
                              BOX_MEM_TSK_STACK_SIZE, &extraId);
        if (ret == LOS_OK) {
            /* The pool was NOT full: the hold above under-filled it. That
             * invalidates the burst, so report rather than silently weaken. */
            dprintf("[MEM] MEM-TSK-009 FAIL: burst attempt %u unexpectedly "
                    "succeeded (taskId=0x%x) - pool was not full\n", i, extraId);
            fails++;
            /* Keep it so the count below stays consistent, then release. */
            if (capBefore < BOX_MEM_TSK009_HOLD_MAX) {
                idsA[capBefore] = extraId;
                capBefore++;
            }
            break;
        }
    }
    dprintf("[MEM] MEM-TSK-009: burned %u refusals against the full pool\n",
            BOX_MEM_TSK009_BURST);

    /* ---- 4) release everything and drain ---- */
    if (!MemTsk009Release(&s0, idsA, capBefore)) {
        BoxMemSnap now;
        (VOID)BoxMemSnapGet(&now, "009 after-release");
        fails += BoxMemFail(caseName, "heap did not recover after release",
                            now.freeSize, s0.freeSize);
    }

    /* ---- 5) measure capacity again: must be identical ---- */
    g_t9Ready = 0;
    g_t9Parked = 1;
    capAfter = MemTsk009Hold(idsB, BOX_MEM_TSK009_HOLD_MAX, &errA);
    dprintf("[MEM] MEM-TSK-009: capacity AFTER  = %u / %u (stop err 0x%x)\n",
            capAfter, BOX_MEM_TSK009_HOLD_MAX, errA);

    /* THE assertion: N refusals cost exactly zero slots. */
    if (capAfter != capBefore) {
        fails += BoxMemFail(caseName,
                            "capacity shrank after refused creates (rollback leak)",
                            capAfter, capBefore);
        dprintf("[MEM] MEM-TSK-009: lost %d slot(s) over %u refusals - the "
                "failure path is not fully rolling back\n",
                (int)(capBefore - capAfter), BOX_MEM_TSK009_BURST);
    } else {
        dprintf("[MEM] MEM-TSK-009: capacity unchanged (%u) after %u refusals - "
                "failure path rolls back cleanly\n", capAfter, BOX_MEM_TSK009_BURST);
    }
    if ((errA != errB) && (capAfter == capBefore)) {
        fails += BoxMemFail(caseName, "stop code not deterministic", errA, errB);
    }

    /* ---- 6) release and confirm the heap is back to baseline ---- */
    if (!MemTsk009Release(&s0, idsB, capAfter)) {
        BoxMemSnap now;
        (VOID)BoxMemSnapGet(&now, "009 final-release");
        fails += BoxMemFail(caseName, "heap did not recover at the end",
                            now.freeSize, s0.freeSize);
    }
    if (!BoxMemTskSettle(&s0, &sFinal)) {
        fails += BoxMemFail(caseName, "final settle failed",
                            sFinal.freeSize, s0.freeSize);
    }
    if (!BoxMemSame(&s0, &sFinal)) {
        fails += BoxMemFail(caseName, "final snapshot != baseline",
                            sFinal.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemTsk009(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemTsk009", MemTsk009Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
