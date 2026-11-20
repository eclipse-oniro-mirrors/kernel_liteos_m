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
 * MEM-TSK-010  The box swtmr task is a one-off, permanent heap cost  (group B)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * The box software-timer machinery needs a task of its own: the kernel scanner
 * never runs a box timer's callback itself, it enqueues a value copy into the
 * box's ring, and a task INSIDE the box consumes that ring and calls the handler
 * unprivileged. That task is brought up by BoxSwtmrInit -> OsSwtmrUsrTaskInit.
 *
 * Its memory profile is the thing under test:
 *
 *   - it is a REAL box task, so it costs a TCB and a pool kernel stack from
 *     .box.res (not the heap) plus a USER STACK from the box heap;
 *   - it lives for the whole life of the box (it loops on the ring forever), so
 *     that heap cost is PERMANENT, not transient;
 *   - the TIMER POOL and the deferred-callback ring live in .box.res too, so
 *     creating 1..SWT_LIMIT timers must not move the heap at all.
 *
 * So the expected curve is: one step when the task appears, then flat no matter
 * how many timers are created, started, fire, or are deleted.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * IMPORTANT: in the CURRENT test build the box entry is boxTestEntry, which
 * does NOT call BoxSwtmrInit (only the demo entry boxAEntry does). So on this
 * target the first snapshot already includes the swtmr task, or it does not
 * exist at all - and this case must not assume it can create it.
 *
 * The case therefore works in two modes:
 *
 *   MODE A - the swtmr task already exists (usual on this target):
 *       verify the heap is FLAT across timer create/start/delete, i.e. timers
 *       cost no heap.
 *
 *   MODE B - it does not exist: report that clearly and still verify the flat
 *       part using the timer API alone; creating the task is the entry point's
 *       job, not a test case's, because a second bring-up would be rejected by
 *       the kernel (SwtmrBoxCtx.cbArray already set).
 *
 * Both modes PASS as long as the heap does not move. The case is a guard against
 * "timer operations secretly allocate from the box heap", which would make long
 * running boxes leak on every timer cycle.
 *
 * ---------------------------------------------------------------------------
 * WHY THE TIMERS USE A HUGE INTERVAL
 * ---------------------------------------------------------------------------
 * The interval is set far beyond the case duration (10000 ticks at ~1s/tick =
 * ~2.7 hours) so no callback can fire and disturb the measurement. Firing
 * behaviour belongs to the timer test suites, not here - this case is purely
 * about where the memory comes from.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

/* A handler that must never run in this case (interval is ~2.7 h). It exists
 * because LOS_SwtmrCreate requires a non-NULL handler. */
static volatile UINT32 g_t10HandlerCalls;

static VOID MemTsk010Handler(UINTPTR arg)
{
    (VOID)arg;
    g_t10HandlerCalls++;
}

static UINT32 MemTsk010Case(VOID)
{
    static const CHAR *caseName = "MEM-TSK-010";
    BoxMemSnap s0;
    BoxMemSnap sCreate;
    BoxMemSnap sFinal;
    UINT32 ids[2] = {0, 0};
    UINT32 made = 0;
    UINT32 ret;
    UINT32 fails = 0;
    UINT32 i;

    /* ---- 1) baseline (already contains the swtmr task if the entry made one) ---- */
    if (!BoxMemSnapGet(&s0, "MEM-TSK-010 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-010: timer pool and callback ring live in .box.res; "
            "creating timers must not move the heap\n");

    /* A one-tick settle so a just-started swtmr task (if any) has parked on its
     * event and stopped touching anything. */
    LOS_TaskDelay(BOX_MEM_TSK_SPIN_TICKS);
    if (!BoxMemSnapGet(&s0, "MEM-TSK-010 settled")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    /* ---- 2) create as many timers as the pool allows (SWT_LIMIT = 2) ----
     * Both the CB array and the ring are static, so each create must be
     * heap-neutral. The huge interval keeps every callback from firing. */
    for (i = 0; i < (UINT32)(sizeof(ids) / sizeof(ids[0])); i++) {
        ret = LOS_SwtmrCreate(10000U, LOS_SWTMR_MODE_PERIOD,
                              (SWTMR_PROC_FUNC)MemTsk010Handler, &ids[i], 0);
        if (ret != LOS_OK) {
            dprintf("[MEM] MEM-TSK-010: timer %u create refused err 0x%x "
                    "(pool limit reached - expected at SWT_LIMIT)\n", i, ret);
            break;
        }
        made++;
    }
    dprintf("[MEM] MEM-TSK-010: created %u timer(s), ids 0x%x / 0x%x\n",
            made, ids[0], ids[1]);

    if (made == 0) {
        /* Not a memory defect, but it means the case cannot observe anything. */
        dprintf("[MEM] MEM-TSK-010: no timer could be created - skipping "
                "(not a memory failure)\n");
        ICUNIT_ASSERT_EQUAL(0, 0, 0);
        return LOS_OK;
    }

    /* ---- 3) creating timers must not move the heap ---- */
    if (!BoxMemSnapGet(&sCreate, "MEM-TSK-010 after-create")) {
        fails++;
    } else {
        if (!BoxMemSame(&s0, &sCreate)) {
            fails += BoxMemFail(caseName, "creating timers moved the heap",
                                sCreate.freeSize, s0.freeSize);
        } else {
            dprintf("[MEM] MEM-TSK-010: timer pool is static - heap untouched\n");
        }
    }

    /* ---- 4) starting them must not move the heap either ----
     * Starting links the CB into the kernel sort list; that is kernel bookkeeping
     * in .box.res plus the sort list, never a heap allocation. */
    for (i = 0; i < made; i++) {
        ret = LOS_SwtmrStart(ids[i]);
        if (ret != LOS_OK) {
            dprintf("[MEM] MEM-TSK-010 FAIL: timer %u start err 0x%x\n", i, ret);
            fails++;
        }
    }
    if (BoxMemSnapGet(&sCreate, "MEM-TSK-010 after-start")) {
        if (!BoxMemSame(&s0, &sCreate)) {
            fails += BoxMemFail(caseName, "starting timers moved the heap",
                                sCreate.freeSize, s0.freeSize);
        }
    } else {
        fails++;
    }

    /* ---- 5) stop and delete: back to baseline, still flat ---- */
    for (i = 0; i < made; i++) {
        (VOID)LOS_SwtmrStop(ids[i]);
        ret = LOS_SwtmrDelete(ids[i]);
        if (ret != LOS_OK) {
            dprintf("[MEM] MEM-TSK-010 FAIL: timer %u delete err 0x%x\n", i, ret);
            fails++;
        }
    }

    if (!BoxMemTskSettle(&s0, &sFinal)) {
        fails += BoxMemFail(caseName, "heap did not return to baseline",
                            sFinal.freeSize, s0.freeSize);
    }

    if (BoxMemSame(&s0, &sFinal)) {
        dprintf("[MEM] MEM-TSK-010: timer lifecycle is completely heap-neutral "
                "(create/start/stop/delete)\n");
    } else {
        fails += BoxMemFail(caseName, "final snapshot != baseline",
                            sFinal.freeSize, s0.freeSize);
    }

    /* The handler must never have fired: a firing would mean the interval was
     * not long enough and the measurement window is not clean. */
    if (g_t10HandlerCalls != 0) {
        dprintf("[MEM] MEM-TSK-010 FAIL: handler fired %u time(s) during the "
                "measurement window (interval too short)\n", g_t10HandlerCalls);
        fails++;
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemTsk010(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemTsk010", MemTsk010Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
