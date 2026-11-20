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
 * MEM-BOX-006  Exhaust the heap, then recover completely  (group A)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * The strongest single test of the allocator's bookkeeping: take the heap down
 * to zero free space, confirm the refusal is CLEAN (NULL, not a corrupt
 * pointer), then give everything back and require the pool to be identical to
 * the baseline - including node counts, so a leaked node header cannot hide.
 *
 * Exhaustion is the state where allocator bugs surface most readily: the free
 * list is at its most fragmented, every split has been taken, and the "no
 * suitable block" path runs for the first time.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - refusal is not NULL      the search returned a garbage node - memory
 *                              corruption, treat as urgent;
 *   - recovery != baseline     freeing everything did not restore the pool:
 *                              a leak, or a merge problem that left fragments;
 *   - recovery freeSize ok but
 *     freeNodes too high       merging is broken; the pool would slowly
 *                              fragment in normal use.
 *
 * The block size is deliberately small (64) so ~150 blocks fit, which drives the
 * free list through many splits instead of just a few big ones.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define BOX_MEM_BOX006_BLKSZ  64U
#define BOX_MEM_BOX006_MAX    200U

/* Pointers are static: 200 entries do not fit comfortably on a 1 KB box stack. */
static VOID *g_box006Blocks[BOX_MEM_BOX006_MAX];

static UINT32 MemBox006Case(VOID)
{
    static const CHAR *caseName = "MEM-BOX-006";
    BoxMemSnap s0;
    BoxMemSnap s1;
    UINT32 got = 0;
    UINT32 fails = 0;
    UINT32 i;

    if (!BoxMemSnapGet(&s0, "006 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    (VOID)memset_s(g_box006Blocks, sizeof(g_box006Blocks), 0, sizeof(g_box006Blocks));

    /* ---- drain the heap ---- */
    while ((got < BOX_MEM_BOX006_MAX) && (g_boxLocalPool[0] != 0)) {
        VOID *p = LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_BOX006_BLKSZ);
        if (p == NULL) {
            break;
        }
        g_box006Blocks[got++] = p;
    }
    BOX_MEM_TRACE("[MEM] %s took %u blocks\n", caseName, got);
    if (got == 0) {
        fails += BoxMemFail(caseName, "could not allocate anything",
                            got, BOX_MEM_BOX006_MAX);
    }

    /* ---- the refusal must be NULL, and repeated calls must stay NULL ---- */
    for (i = 0; i < 3U; i++) {
        VOID *p = LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_BOX006_BLKSZ);
        if (p != NULL) {
            fails += BoxMemFail(caseName, "over-large alloc returned non-NULL",
                                (UINT32)(UINTPTR)p, 0U);
            (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], p);
        }
    }

    /* A request bigger than the whole pool must also be refused cleanly. */
    if (LOS_MemAlloc((VOID *)g_boxLocalPool[0], s0.poolSize) != NULL) {
        fails += BoxMemFail(caseName, "pool-sized alloc returned non-NULL",
                            s0.poolSize, 0U);
    }

    /* ---- give everything back ---- */
    for (i = 0; i < got; i++) {
        if (g_box006Blocks[i] != NULL) {
            (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], g_box006Blocks[i]);
            g_box006Blocks[i] = NULL;
        }
    }

    if (!BoxMemSnapGet(&s1, "006 final")) {
        fails++;
    } else {
        BOX_MEM_TRACE("[MEM] %s recovered free=%u/%u nodes=%u/%u maxFree=%u/%u\n",
                      caseName, s1.freeSize, s0.freeSize, s1.freeNodes,
                      s0.freeNodes, s1.maxFree, s0.maxFree);
        if (!BoxMemSame(&s0, &s1)) {
            fails += BoxMemFail(caseName, "recovery != baseline",
                                s1.freeSize, s0.freeSize);
        }
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMem006(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMem006", MemBox006Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
