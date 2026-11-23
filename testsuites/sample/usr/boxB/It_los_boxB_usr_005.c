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
 * BOX-B-005  Exhaust boxB's heap, fragment it, then recover  (boxB group)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * boxB's heap (~12K) is the biggest single block of test-controlled memory in
 * the image, so it is the right place to drive the allocator into its worst
 * states and prove they are all recoverable:
 *
 *   phase 1  DRAIN   allocate small blocks until the pool refuses. The refusal
 *                    must be a clean NULL, repeated calls must stay NULL, and a
 *                    request larger than the whole pool must also be NULL.
 *   phase 2  FRAGMENT free every other block, so the total free space is large
 *                    but no single hole is. A request bigger than maxFree must
 *                    fail even though freeSize says there is room - that is the
 *                    documented behaviour of a non-moving allocator, not a bug.
 *   phase 3  RECOVER  free everything and require the pool to be identical to
 *                    the entry snapshot, including maxFree and the node counts.
 *                    This is the part that proves fragmentation is TRANSIENT:
 *                    merging reconstructs one big hole.
 *
 * ---------------------------------------------------------------------------
 * WHY boxB AND NOT boxA
 * ---------------------------------------------------------------------------
 * Draining boxA's heap would leave the shared suite unable to spawn the tasks
 * its later cases need. boxB has no other consumer, so the drain is safe here.
 *
 * ---------------------------------------------------------------------------
 * WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - over-large request returned non-NULL   corruption: the search handed back
 *                                            a node it cannot possibly satisfy
 *   - accounting broke after the refusal     the no-fit path left the pool
 *                                            half-updated
 *   - recovery != baseline                   merging failed: the pool decays
 *                                            over time, the worst kind of bug
 *                                            for a long-running box
 *   - recovery freeSize ok but maxFree wrong merging is missing; total free is
 *                                            right but it is all fragments
 * ========================================================================== */

#include "It_los_boxB_usr_util.h"

#define B005_BLK    48U
/* Generous bound, NOT a target: the pool must be what stops the drain loop, so
 * the array has to be larger than anything the heap can satisfy. 10620 bytes of
 * heap / 64 bytes per 48-byte block = at most ~166 blocks, so 256 is ample and
 * costs 1 KB of .boxB.bss instead of 1.6 KB (that bss comes straight out of the
 * box heap - they share the 16K BOX_B_RAM slot). */
#define B005_MAX    256U

/* static: 256 pointers do not fit on a 1K box stack */
static VOID *g_b005Blk[B005_MAX];

static UINT32 MemB005Case(VOID)
{
    static const CHAR *caseName = "BOX-B-005";
    BoxBSnap s0;
    BoxBSnap sMid;
    BoxBSnap s1;
    VOID *pool;
    UINT32 got = 0;
    UINT32 fails = 0;
    UINT32 i;
    VOID *big = NULL;

    if (!BoxBSnapGet(&s0)) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    pool = (VOID *)s0.pool;
    (VOID)memset_s(g_b005Blk, sizeof(g_b005Blk), 0, sizeof(g_b005Blk));

    /* ---- phase 1: drain ---- */
    while (got < B005_MAX) {
        VOID *p = LOS_MemAlloc(pool, B005_BLK);

        if (p == NULL) {
            break;
        }
        g_b005Blk[got++] = p;
    }
    dprintf("[BOXB] %s: drained with %u block(s) of %u bytes\n",
            caseName, got, B005_BLK);
    if (got == 0U) {
        fails += BoxBFail(caseName, "could not allocate anything", got, 1U);
        goto OUT;
    }

    /* repeated refusals must stay refusals, and a pool-sized request too */
    for (i = 0; i < 3U; i++) {
        VOID *p = LOS_MemAlloc(pool, B005_BLK);

        if (p != NULL) {
            fails += BoxBFail(caseName, "over-large alloc returned non-NULL",
                              (UINT32)(UINTPTR)p, 0U);
            (VOID)LOS_MemFree(pool, p);
        }
    }
    if (LOS_MemAlloc(pool, s0.poolSize) != NULL) {
        fails += BoxBFail(caseName, "pool-sized alloc returned non-NULL",
                          s0.poolSize, 0U);
    }

    /* ---- phase 2: fragment (free every other block) ---- */
    for (i = 0; i < got; i += 2U) {
        (VOID)LOS_MemFree(pool, g_b005Blk[i]);
        g_b005Blk[i] = NULL;
    }
    if (!BoxBSnapGet(&sMid)) {
        fails += BoxBFail(caseName, "snapshot after fragmenting refused", 0U, 1U);
        goto OUT;
    }
    dprintf("[BOXB] %s: fragmented: free=%u maxFree=%u (holes smaller than total)\n",
            caseName, sMid.freeSize, sMid.maxFree);

    /* accounting must still add up with a fragmented free list */
    if ((sMid.used + sMid.freeSize + BOXB_SENTINEL_SIZE) != sMid.poolSize) {
        fails += BoxBFail(caseName, "accounting broke when fragmented",
                          sMid.used + sMid.freeSize + BOXB_SENTINEL_SIZE,
                          sMid.poolSize);
    }
    /* a request just past the largest hole must be refused */
    if (sMid.maxFree > BOXB_NODE_HEAD_SIZE) {
        big = LOS_MemAlloc(pool, sMid.maxFree + 4U);
        if (big != NULL) {
            fails += BoxBFail(caseName, "request past maxFree unexpectedly served",
                              (UINT32)(UINTPTR)big, 0U);
            (VOID)LOS_MemFree(pool, big);
        }
    }

    /* ---- phase 3: release the survivors and require full recovery ---- */
    for (i = 1; i < got; i += 2U) {
        if (g_b005Blk[i] != NULL) {
            (VOID)LOS_MemFree(pool, g_b005Blk[i]);
            g_b005Blk[i] = NULL;
        }
    }
    if (!BoxBSnapGet(&s1)) {
        fails += BoxBFail(caseName, "final snapshot refused", 0U, 1U);
        goto OUT;
    }
    dprintf("[BOXB] %s: recovered: free=%u maxFree=%u nodes=%u/%u\n",
            caseName, s1.freeSize, s1.maxFree, s1.freeNodes, s1.usedNodes);
    if (!BoxBSame(&s0, &s1)) {
        fails += BoxBFail(caseName, "merging did not restore the baseline "
                          "(fragmentation would be permanent)",
                          s1.maxFree, s0.maxFree);
    }

OUT:
    for (i = 0; i < B005_MAX; i++) {
        if (g_b005Blk[i] != NULL) {
            (VOID)LOS_MemFree(pool, g_b005Blk[i]);
            g_b005Blk[i] = NULL;
        }
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxBUsr005(VOID)
{
    TEST_ADD_CASE("ItLosBoxBUsr005", MemB005Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
