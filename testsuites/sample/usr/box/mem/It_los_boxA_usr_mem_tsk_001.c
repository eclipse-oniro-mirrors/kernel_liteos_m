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
 * MEM-TSK-001  Heap cost of creating one runtime task   (group B)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * A box task's memory comes from THREE different places, and only one of them is
 * the box heap. The split is the whole point of the design:
 *
 *     TCB           176 B   <- box TCB pool         (.box.res, NOT the heap)
 *     kernel stack 1032 B   <- box kernel-stack pool (.box.res, NOT the heap)
 *     user stack    ~1 KB   <- BOX HEAP             (the only heap consumer)
 *
 * Because the first two are static pools, creating one box task must grow the
 * heap by EXACTLY the cost of its user stack and nothing else. That makes the
 * assertion precise rather than approximate - there is no legitimate slack:
 *
 *     uwStackSize 1024, alignment 8
 *       useSize   = 1024 + 8 - sizeof(gapSize=4)      = 1028
 *       allocSize = ALIGN(1028 + node header 16, 4)   = 1044
 *
 * The expected value is COMPUTED by BoxMemAlignAllocCost(), not hard-coded, so a
 * configuration change (alignment, stack size, node header) is followed
 * automatically. Both the expected and the measured number are printed.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 * The delta is a fingerprint of the seam:
 *
 *     delta == 1044           seam intact
 *     delta == 1044 + 176     the TCB came from the heap
 *     delta == 1044 + 1032    the kernel stack came from the heap
 *     delta == 1044 + 1208    both did
 *     delta other             alignment / node header assumption is wrong
 *
 * The case also checks that exactly ONE new used node appeared: two nodes for one
 * task would mean something else was carved out of the same pool.
 *
 * Teardown is asserted as well - the child must run, exit and have its stack
 * drained before the heap returns to the baseline. So this case proves the
 * create path leaves no residue, not just that it allocates the right amount.
 *
 * NOTE the user stack is real memory: the probe writes a pattern into its own
 * stack region before returning, which would fault immediately if the box had
 * been handed an address outside its data slot.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

/* Case-local probe state: `static` keeps them out of the symbol table and stops
 * two cases from sharing a flag. */
static volatile UINT32 g_t1Ran;      /* set by the probe body     */
static volatile UINT32 g_t1Alive;    /* probes still running      */
static volatile UINT32 g_t1StackOk;  /* probe's stack self-check  */

/* Probe body.
 *
 * Two jobs: touch its own user stack (proving the box owns writable memory
 * there), then RETURN. The return is deliberate - a box task's initial LR is
 * OsUsrTaskExit, so returning issues the exit syscall and the kernel deletes the
 * task through the encoded-id path, which is the real usage we want to measure.
 */
static VOID MemTsk001Entry(VOID)
{
    UINT8 probe[64];

    g_t1StackOk = BoxMemPatternCheck(probe, (UINT32)sizeof(probe), 0xA5A50001U);
    g_t1Ran = 1;
    g_t1Alive--;
    /* return -> OsUsrTaskExit trampoline -> detached delete (kernel side) */
}

static UINT32 MemTsk001Case(VOID)
{
    static const CHAR *caseName = "MEM-TSK-001";
    const UINT32 boundary = (UINT32)LOSCFG_STACK_POINT_ALIGN_SIZE;
    const UINT32 expectedCost = BoxMemAlignAllocCost(BOX_MEM_TSK_STACK_SIZE, boundary);
    BoxMemSnap s0;
    BoxMemSnap s1;
    BoxMemSnap s2;
    UINT32 taskId = 0;
    UINT32 delta;
    UINT32 ret;
    UINT32 fails = 0;

    /* ---- 1) baseline ---- */
    if (!BoxMemSnapGet(&s0, "MEM-TSK-001 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-001: user stack %u, boundary %u -> expected heap cost %u "
            "(ALIGN(%u + %u - %u + %u, %u))\n",
            BOX_MEM_TSK_STACK_SIZE, boundary, expectedCost,
            BOX_MEM_TSK_STACK_SIZE, boundary, (UINT32)sizeof(UINT32),
            BOX_MEM_NODE_HEAD_SIZE, (UINT32)OS_MEM_ALIGN_SIZE);

    /* ---- 2) create one runtime box task ---- */
    g_t1Ran = 0;
    g_t1Alive = 1;
    g_t1StackOk = 0;

    ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk001Entry, "memTsk001",
                          BOX_MEM_TSK_STACK_SIZE, &taskId);
    if (ret != LOS_OK) {
        g_t1Alive = 0;
        /* No task means no delta. The error code is the diagnosis:
         * TCB_UNAVAILABLE = TCB pool empty, NO_MEMORY = heap or stack pool. */
        dprintf("[MEM] MEM-TSK-001 FAIL: LOS_TaskCreate err 0x%x\n", ret);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-001: child taskId=0x%x created\n", taskId);

    /* ---- 3) measure the heap delta immediately after creation ---- */
    if (!BoxMemSnapGet(&s1, "MEM-TSK-001 after-create")) {
        fails++;
    } else {
        if (s1.freeSize > s0.freeSize) {
            /* Free space grew after allocating: impossible, and the unsigned
             * subtraction below would wrap. Flag it explicitly. */
            fails += BoxMemFail(caseName, "free grew after create",
                                s1.freeSize, s0.freeSize);
        } else {
            delta = s0.freeSize - s1.freeSize;
            dprintf("[MEM] MEM-TSK-001: measured heap delta = %u (expect %u)\n",
                    delta, expectedCost);
            if (delta != expectedCost) {
                /* Name the culprits so a regression explains itself. */
                dprintf("[MEM] MEM-TSK-001: hint - delta %u vs expected %u; "
                        "surplus 176 = TCB from heap, ~1032 = kernel stack from "
                        "heap, 1208 = both\n", delta, expectedCost);
                fails += BoxMemFail(caseName, "heap delta != user-stack cost",
                                    delta, expectedCost);
            }
            /* Exactly one new used node: the user stack. */
            if (s1.usedNodes != s0.usedNodes + 1U) {
                fails += BoxMemFail(caseName, "usedNodes did not grow by exactly 1",
                                    s1.usedNodes, s0.usedNodes + 1U);
            }
        }
    }

    /* ---- 4) the child must run, and its stack must have been writable ---- */
    if (!BoxMemTskWaitFlag(&g_t1Ran, 1)) {
        dprintf("[MEM] MEM-TSK-001 FAIL: child never ran (prio/starvation?)\n");
        fails++;
    } else if (g_t1StackOk != 1) {
        /* Reachable only if the box was given a stack it cannot actually write -
         * which the MPU would normally turn into a fault, so this is belt and
         * braces. */
        dprintf("[MEM] MEM-TSK-001 FAIL: child could not write its own stack\n");
        fails++;
    } else {
        dprintf("[MEM] MEM-TSK-001: child wrote its own user stack correctly "
                "(stack really belongs to this box)\n");
    }
    (VOID)BoxMemTskWaitFlag(&g_t1Alive, 0);

    /* ---- 5) exit + drain must return the heap to the baseline ---- */
    if (!BoxMemTskSettle(&s0, &s2)) {
        fails += BoxMemFail(caseName, "heap did not return to baseline",
                            s2.freeSize, s0.freeSize);
    }

    if (BoxMemSame(&s0, &s2)) {
        dprintf("[MEM] MEM-TSK-001: create -> exit -> drain is balanced "
                "(no residue)\n");
    } else {
        fails += BoxMemFail(caseName, "final snapshot != baseline",
                            s2.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemTsk001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemTsk001", MemTsk001Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
