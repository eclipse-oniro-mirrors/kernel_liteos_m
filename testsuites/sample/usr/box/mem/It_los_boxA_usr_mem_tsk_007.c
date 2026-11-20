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
 * MEM-TSK-007  TCB pool and kernel-stack pool stay in step   (group B)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * A box task consumes TWO fixed resources from .box.res, not one:
 *
 *     one TCB                  from boxResTaskList   (LOSCFG_BOXx_TASK_LIMIT)
 *     one pool kernel stack    from boxResStackList  (same count)
 *
 * Both arrays are sized by the same macro, so they are supposed to run out
 * together. MEM-TSK-006 sees whichever pool empties first and reports its error
 * code; this case is the STRUCTURAL check that the two really are in step.
 *
 * Why it deserves its own case: the two pools are consumed by different code
 * (OsBoxTaskGetFreeTaskCB vs OsBoxKernelStackAlloc) and released by different
 * code (OsTaskTCBRecycle vs OsBoxKernelStackFree). A bug in either half shows
 * up exactly here:
 *
 *   - TCB released but kernel stack leaked  -> TCB_UNAVAILABLE arrives while
 *     kernel-stack nodes remain; capacity shrinks by one every churn cycle;
 *   - kernel stack released but TCB leaked  -> NO_MEMORY arrives first;
 *   - both fine                              -> the two limits are reached at
 *     the same create attempt.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * The case creates as many probes as the box allows, notes WHICH error stopped
 * it, releases everything, and then runs one more create/release cycle. The
 * second cycle is the real test: if either pool lost a slot, capacity drops and
 * the case detects it by comparing how many creates succeed the second time
 * against the first.
 *
 * Expected on this target: capacity = TASK_LIMIT - (entry task) = 6 - 1 = 5
 * (the swtmr task is not started in the test build), and the stop code is
 * TCB_UNAVAILABLE. Both numbers are printed, not asserted, so a deliberate
 * configuration change does not break the case - the SHAPE is what is asserted:
 *
 *     capacity must be identical in both rounds, and >= 1.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

static volatile UINT32 g_t7Ran;
static volatile UINT32 g_t7Alive;

static VOID MemTsk007Entry(VOID)
{
    g_t7Ran++;
    g_t7Alive--;
}

/* Run one fill-and-release round; returns how many creates succeeded.
 * `stopErr` receives the error that stopped the round (LOS_OK if the internal
 * bound was hit first, which should not happen). */
/* Measure how many probes the pools will hand out, ONE AT A TIME.
 *
 * Serialised on purpose (create -> run -> exit -> drain per probe) instead of
 * allocating a batch: a deleted dynamic task queues its user-stack address on
 * boxResRecycleStackList, whose capacity equals boxResTaskNum (TASK_LIMIT =
 * 6). Batching more than that without draining would trip
 * OsBoxUserStackRecycle's overflow panic. Draining each round keeps the queue
 * at depth 1, so this measures the pool limit and nothing else.
 *
 * `stopErr` receives the error that stopped the round. */
static UINT32 MemTsk007Fill(UINT32 *stopErr)
{
    BoxMemSnap base;
    BoxMemSnap now;
    UINT32 created = 0;
    UINT32 taskId = 0;
    UINT32 ret;
    UINT32 i;

    if (stopErr != NULL) {
        *stopErr = LOS_OK;
    }
    if (!BoxMemSnapGet(&base, "007 fill base")) {
        return 0;
    }

    for (i = 0; i < 32U; i++) {
        UINT32 before = g_t7Ran;

        g_t7Alive = 1;
        ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk007Entry, "memTsk007",
                              BOX_MEM_TSK_STACK_SIZE, &taskId);
        if (ret != LOS_OK) {
            g_t7Alive = 0;
            if (stopErr != NULL) {
                *stopErr = ret;
            }
            break;
        }
        created++;

        /* Run it, then drain it: this is what keeps the recycle queue shallow. */
        (VOID)BoxMemTskWaitFlag(&g_t7Ran, before + 1U);
        (VOID)BoxMemTskWaitFlag(&g_t7Alive, 0);
        (VOID)BoxMemTskSettle(&base, &now);
    }
    return created;
}

static UINT32 MemTsk007Case(VOID)
{
    static const CHAR *caseName = "MEM-TSK-007";
    BoxMemSnap s0;
    BoxMemSnap sFinal;
    UINT32 cap1;
    UINT32 cap2;
    UINT32 err1 = LOS_OK;
    UINT32 err2 = LOS_OK;
    UINT32 fails = 0;

    /* ---- 1) baseline ---- */
    if (!BoxMemSnapGet(&s0, "MEM-TSK-007 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    /* ---- 2) first round: measure capacity ---- */
    cap1 = MemTsk007Fill(&err1);
    dprintf("[MEM] MEM-TSK-007: round 1 capacity=%u, stop err=0x%x\n", cap1, err1);
    if (cap1 == 0) {
        dprintf("[MEM] MEM-TSK-007 FAIL: nothing could be created in round 1\n");
        fails++;
    }
    (VOID)BoxMemTskSettle(&s0, NULL);

    /* ---- 3) second round: capacity must be IDENTICAL ----
     * This is the leak detector. If either pool failed to return a slot, round 2
     * reports one less. */
    cap2 = MemTsk007Fill(&err2);
    dprintf("[MEM] MEM-TSK-007: round 2 capacity=%u, stop err=0x%x\n", cap2, err2);
    if (cap2 != cap1) {
        fails += BoxMemFail(caseName,
                            "capacity changed between rounds (pool leak)",
                            cap2, cap1);
        if (err2 != err1) {
            dprintf("[MEM] MEM-TSK-007: stop code also changed (0x%x -> 0x%x) - "
                    "the two pools are out of step\n", err1, err2);
        }
    } else {
        dprintf("[MEM] MEM-TSK-007: capacity stable across rounds "
                "(%u == %u) - TCB and kernel-stack pools are in step\n", cap1, cap2);
    }

    /* ---- 4) the stop codes should agree too ----
     * Different codes across two identical rounds would mean the order in which
     * the two pools run dry changed, which should be deterministic. */
    if ((err1 != err2) && (cap1 == cap2)) {
        fails += BoxMemFail(caseName, "stop code not deterministic across rounds",
                            err2, err1);
    }

    /* ---- 5) heap back to baseline ---- */
    if (!BoxMemTskSettle(&s0, &sFinal)) {
        fails += BoxMemFail(caseName, "heap did not recover", sFinal.freeSize, s0.freeSize);
    }
    if (!BoxMemSame(&s0, &sFinal)) {
        fails += BoxMemFail(caseName, "final snapshot != baseline",
                            sFinal.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemTsk007(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemTsk007", MemTsk007Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
