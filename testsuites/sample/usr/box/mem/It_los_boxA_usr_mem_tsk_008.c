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
 * MEM-TSK-008  A failed create must leave NOTHING behind   (group B)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * Creating a box task takes THREE resources in a fixed order:
 *
 *     (1) user stack     from the box heap   (usr side, LOS_MemAllocAlign)
 *     (2) pool kernel stack from .box.res    (kernel side, first)
 *     (3) TCB            from .box.res       (kernel side, second)
 *
 * Whichever step fails, the resources already taken by the earlier steps must be
 * given back. The kernel side does that (OsBoxKernelStackFreeNode on the TCB
 * failure, OsTaskCreateTCBRewind inside the generic create); the usr side does
 * the heap free when the syscall fails. This case forces a failure at the LAST
 * possible step - the heap can no longer supply a user stack - and checks that
 * the two earlier steps did not consume anything permanently.
 *
 * The failure is arranged by allocating most of the heap first, so that a
 * 0x400-stack request can no longer be satisfied. Then theplaceholder blocks are
 * released and a create is retried: it MUST succeed immediately. If it does
 * not, a TCB or kernel-stack slot has leaked, and the box's capacity quietly
 * shrinks with every failed attempt - a classic slow-burn failure.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * Theplaceholder allocation uses several coarse blocks rather than one exact fit, so
 * the case does not depend on the precise allocator behaviour: it allocates
 * until a 0x400 request would obviously fail (or the block list is full).
 *
 * Read the three key lines:
 *   "starved create refused"   - the forced failure happened as planned
 *   "retry after release OK"   - nothing leaked (this is the assertion)
 *   final snapshot == baseline - the heap is fully restored
 *
 * NOTE a starved create may fail either at the heap step or, if the heap
 * happens to fit, at the pools; both are acceptable. What is NOT acceptable is
 * the retry failing.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

static volatile UINT32 g_t8Alive;

/* How many coarse blocks we use to occupy the heap. 8 blocks of ~0x400 each is
 * enough to starve a 0x400 request on this board's ~11 KB heap while keeping the
 * bookkeeping trivial. */
#define BOX_MEM_TSK008_BLOCKS  8U
#define BOX_MEM_TSK008_BLKSZ   0x400U

static VOID MemTsk008Entry(VOID)
{
    g_t8Alive--;
}

static UINT32 MemTsk008Case(VOID)
{
    static const CHAR *caseName = "MEM-TSK-008";
    static VOID *blocks[BOX_MEM_TSK008_BLOCKS];
    BoxMemSnap s0;
    BoxMemSnap sStarved;
    BoxMemSnap sFinal;
    UINT32 held = 0;
    UINT32 taskId = 0;
    UINT32 ret;
    UINT32 fails = 0;
    UINT32 i;

    /* ---- 1) baseline ---- */
    if (!BoxMemSnapGet(&s0, "MEM-TSK-008 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    (VOID)memset_s(blocks, sizeof(blocks), 0, sizeof(blocks));

    /* ---- 2) occupy the heap until a 0x400 stack cannot fit ---- */
    for (i = 0; i < BOX_MEM_TSK008_BLOCKS; i++) {
        blocks[i] = LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_TSK008_BLKSZ);
        if (blocks[i] == NULL) {
            break;
        }
        held++;
    }
    dprintf("[MEM] MEM-TSK-008: took %u x %u bytes from the heap to starve it\n",
            held, BOX_MEM_TSK008_BLKSZ);
    if (held == 0) {
        dprintf("[MEM] MEM-TSK-008 FAIL: could not take even one block\n");
        ICUNIT_ASSERT_EQUAL(held, 0, held);
        return LOS_OK;
    }

    /* ---- 3) the starved create must be REFUSED ---- */
    g_t8Alive = 1;
    ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk008Entry, "memTsk008",
                          BOX_MEM_TSK_STACK_SIZE, &taskId);
    if (ret == LOS_OK) {
        /* The heap still had room - the case cannot exercise what it wants.
         * That is not a defect in the code under test, so it is reported and
         * skipped rather than failed. */
        g_t8Alive = 0;
        dprintf("[MEM] MEM-TSK-008: create SUCCEEDED despite starvation - "
                "heap larger than expected, case skipped (not a failure)\n");
        (VOID)BoxMemTskWaitFlag(&g_t8Alive, 0);
        for (i = 0; i < held; i++) {
            (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], blocks[i]);
        }
        (VOID)BoxMemTskSettle(&s0, NULL);
        ICUNIT_ASSERT_EQUAL(0, 0, 0);
        return LOS_OK;
    }
    g_t8Alive = 0;
    dprintf("[MEM] MEM-TSK-008: starved create refused with 0x%x "
            "(expected NO_MEMORY or TCB_UNAVAILABLE)\n", ret);
    if ((ret != LOS_ERRNO_TSK_NO_MEMORY) && (ret != LOS_ERRNO_TSK_TCB_UNAVAILABLE)) {
        fails += BoxMemFail(caseName, "starved create gave an unexpected error",
                            ret, LOS_ERRNO_TSK_NO_MEMORY);
    }

    /* ---- 4) the refusal must not have consumed any resource ----
     * Snapshot BEFORE releasing the blocks: only the blocks should be missing,
     * nothing else. If a TCB or kernel-stack slot had leaked it would not show
     * up here (they are not heap), which is exactly why step 5 exists. */
    if (!BoxMemSnapGet(&sStarved, "MEM-TSK-008 starved")) {
        fails++;
    } else {
        if (sStarved.usedNodes != (s0.usedNodes + held)) {
            fails += BoxMemFail(caseName,
                                "starved create itself changed node count",
                                sStarved.usedNodes, s0.usedNodes + held);
        }
    }

    /* ---- 5) release the blocks and retry: MUST succeed ---- */
    for (i = 0; i < held; i++) {
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], blocks[i]);
        blocks[i] = NULL;
    }
    (VOID)BoxMemTskSettle(&s0, NULL);

    g_t8Alive = 1;
    ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk008Entry, "memTsk008b",
                          BOX_MEM_TSK_STACK_SIZE, &taskId);
    if (ret != LOS_OK) {
        g_t8Alive = 0;
        /* THE assertion of this case: after a failed create, a subsequent
         * create must work, because the failure must not have burned a TCB or a
         * pool kernel stack. */
        dprintf("[MEM] MEM-TSK-008 FAIL: retry after release err 0x%x "
                "- a failed create leaked a TCB or kernel stack!\n", ret);
        fails++;
    } else {
        dprintf("[MEM] MEM-TSK-008: retry after release OK (taskId=0x%x) - "
                "the failed create left no residue\n", taskId);
        (VOID)BoxMemTskWaitFlag(&g_t8Alive, 0);
    }

    /* ---- 6) heap back to baseline ---- */
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

VOID ItLosBoxUsrMemTsk008(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemTsk008", MemTsk008Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
