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
 * MEM-TSK-002  User stack is recycled in TWO stages   (group B)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * When a box task dies its memory does NOT all come back at once. The design is
 * deliberately asymmetric:
 *
 *   IMMEDIATELY, kernel side (OsTaskStackFree -> OsTaskResRelease):
 *       - TCB          -> box TCB pool free list
 *       - kernel stack -> box kernel-stack pool free list
 *       - user stack   -> only its ADDRESS is pushed onto
 *                         boxResRecycleStackList (a queue); the bytes are NOT
 *                         freed here
 *
 *   LATER, user side (LOS_TaskStackRecycle -> LOS_MemFree):
 *       - each queued address is handed back to the box heap
 *
 * WHY the asymmetry: the user stack cannot be freed at delete time. A task
 * deleting itself still has its live context on that stack, so freeing it there
 * would be self-cannibalisation; and a delete can also happen inside a syscall,
 * where pending the box heap's mux would risk deadlock. The kernel therefore
 * only records the address and the box drains it from a safe point.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT IT PROVES
 * ---------------------------------------------------------------------------
 * The case makes the two stages separately observable:
 *
 *   1. baseline snapshot;
 *   2. create a probe task -> heap drops by its user stack;
 *   3. wait for the probe to RETURN and be deleted;
 *   4. snapshot WITHOUT draining:
 *          the heap must STILL be short by exactly one stack (stage 1 did NOT
 *          free it), yet the TCB must already be reusable;
 *   5. create a second probe of the SAME size -> must SUCCEED, which is the
 *      proof that the TCB (and pool kernel stack) came back; heap drops again;
 *   6. drain -> both stacks return;
 *   7. final snapshot must equal the baseline.
 *
 * If step 4 shows the heap already recovered, something is freeing the user
 * stack early - that is a bug, the stack may still be in use.
 * If step 5 fails with TSK_TCB_UNAVAILABLE, the TCB was not recycled - also a
 * bug, and a leak that a create/exit loop would eventually hit.
 *
 * This is the single most important memory invariant of the BOX design, which
 * is why it gets its own case.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

/* Case-local probe state. `static` on purpose: no link-time symbol, and two
 * cases can never share a flag by accident. */
static volatile UINT32 g_t2Ran;      /* set by the probe body           */
static volatile UINT32 g_t2Alive;    /* probes still running            */

/* Probe body: raise the flag, then RETURN. The return is what triggers the exit
 * trampoline (a box task's initial LR is OsUsrTaskExit) and therefore the
 * delete path this case is about. */
static VOID MemTsk002Entry(VOID)
{
    g_t2Ran = 1;
    g_t2Alive--;
}

static UINT32 MemTsk002Case(VOID)
{
    static const CHAR *caseName = "MEM-TSK-002";
    const UINT32 stackCost = BoxMemAlignAllocCost(BOX_MEM_TSK_STACK_SIZE,
                                                  (UINT32)LOSCFG_STACK_POINT_ALIGN_SIZE);
    BoxMemSnap s0;
    BoxMemSnap sDel;
    BoxMemSnap sRec;
    BoxMemSnap sFinal;
    UINT32 taskIdA = 0;
    UINT32 taskIdB = 0;
    UINT32 ret;
    UINT32 fails = 0;

    /* ---- 1) baseline ---- */
    if (!BoxMemSnapGet(&s0, "MEM-TSK-002 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-002: one %u-byte user stack costs %u heap bytes\n",
            BOX_MEM_TSK_STACK_SIZE, stackCost);

    /* ---- 2) first probe ---- */
    g_t2Ran = 0;
    g_t2Alive = 1;
    ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk002Entry, "memTsk002A",
                          BOX_MEM_TSK_STACK_SIZE, &taskIdA);
    if (ret != LOS_OK) {
        g_t2Alive = 0;
        dprintf("[MEM] MEM-TSK-002 FAIL: first create err 0x%x\n", ret);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-002: probe A created taskId=0x%x\n", taskIdA);

    /* ---- 3) wait for it to run and exit ---- */
    if (!BoxMemTskWaitFlag(&g_t2Ran, 1)) {
        dprintf("[MEM] MEM-TSK-002 FAIL: probe A body never started\n");
        fails++;
    }
    (VOID)BoxMemTskWaitFlag(&g_t2Alive, 0);
    /* One tick so the kernel's delete / TCB recycle completes. This is NOT the
     * drain: LOS_TaskStackRecycle is deliberately withheld in this step. */
    LOS_TaskDelay(BOX_MEM_TSK_SPIN_TICKS);

    /* ---- 4) WITHOUT draining: heap must still miss exactly one stack ---- */
    if (!BoxMemSnapGet(&sDel, "MEM-TSK-002 after-del")) {
        fails++;
    } else {
        if (sDel.freeSize != (s0.freeSize - stackCost)) {
            /* Two distinct mistakes land here:
             *   freeSize == baseline -> freed too early (dangerous);
             *   anything else         -> a different amount went missing. */
            fails += BoxMemFail(caseName,
                                "stage1: heap must still miss exactly one stack",
                                sDel.freeSize, s0.freeSize - stackCost);
        } else {
            dprintf("[MEM] MEM-TSK-002: stage1 OK - stack NOT freed yet "
                    "(free=%u, baseline=%u)\n", sDel.freeSize, s0.freeSize);
        }
    }

    /* ---- 5) recreate WITHOUT draining: must succeed (TCB is back) ---- */
    g_t2Alive = 1;
    ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk002Entry, "memTsk002B",
                          BOX_MEM_TSK_STACK_SIZE, &taskIdB);
    if (ret != LOS_OK) {
        g_t2Alive = 0;
        /* The interesting failure: TCB or pool kernel stack was not recycled. */
        dprintf("[MEM] MEM-TSK-002 FAIL: recreate without drain err 0x%x "
                "(TCB / kernel stack not recycled?)\n", ret);
        fails++;
    } else {
        dprintf("[MEM] MEM-TSK-002: probe B created taskId=0x%x "
                "(TCB reusable without draining - expected)\n", taskIdB);
        if (BoxMemSnapGet(&sRec, "MEM-TSK-002 after-rec ")) {
            if (sRec.freeSize != (s0.freeSize - (2U * stackCost))) {
                fails += BoxMemFail(caseName,
                                    "two live stacks must cost exactly two stacks",
                                    sRec.freeSize, s0.freeSize - (2U * stackCost));
            }
        } else {
            fails++;
        }
        (VOID)BoxMemTskWaitFlag(&g_t2Alive, 0);
    }

    /* ---- 6) drain: both stacks come back ---- */
    if (!BoxMemTskSettle(&s0, &sFinal)) {
        fails += BoxMemFail(caseName, "stage2: drain did not restore baseline",
                            sFinal.freeSize, s0.freeSize);
    }

    /* ---- 7) final state == baseline ---- */
    if (!BoxMemSame(&s0, &sFinal)) {
        fails += BoxMemFail(caseName, "final snapshot != baseline",
                            sFinal.freeSize, s0.freeSize);
    } else {
        dprintf("[MEM] MEM-TSK-002: stage2 OK - drain returned both stacks\n");
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemTsk002(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemTsk002", MemTsk002Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
