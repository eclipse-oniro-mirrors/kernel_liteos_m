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
 * MEM-BOX-005  Coalescing is order independent (both directions)  (group A)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * bestfit merges a freed node with its free NEIGHBOURS. Merging can happen
 * towards the lower address, towards the higher one, or both - and the three
 * paths are different code. A bug in one of them shows up as fragmentation that
 * only appears for particular free orders, which is exactly the kind of defect
 * that survives casual testing.
 *
 * The case therefore frees three adjacent blocks in TWO orders and requires the
 * pool to be identical to the baseline after each:
 *
 *     order 1:  free(c), free(b), free(a)    - merge downwards each time
 *     order 2:  free(a), free(b), free(c)    - merge upwards each time
 *
 * maxFree is the interesting observable: after the first free only one block is
 * back, after the second two are merged, after the third the whole payload is
 * one block again. Comparing the final snapshot to the baseline (which includes
 * maxFree) is what proves the merge really reconstructed the original block
 * rather than just accumulating free bytes.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * Run it; a failure names the order that broke. If only one order fails, the
 * corresponding merge direction is the suspect. If the final `freeSize` matches
 * but `maxFree` does not, the merge is missing entirely and the pool has become
 * fragmented with the same total free space.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define BOX_MEM_BOX005_BLKSZ  64U

/* Allocate three adjacent blocks, free them in a given order, compare. */
static UINT32 BoxMem005Round(UINT32 order, const CHAR *caseName)
{
    BoxMemSnap s0;
    BoxMemSnap s1;
    VOID *a = NULL;
    VOID *b = NULL;
    VOID *c = NULL;

    if (!BoxMemSnapGet(&s0, "005 round")) {
        return 1U;
    }

    a = LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_BOX005_BLKSZ);
    b = LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_BOX005_BLKSZ);
    c = LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_BOX005_BLKSZ);
    if ((a == NULL) || (b == NULL) || (c == NULL)) {
        return BoxMemFail(caseName, "three-block alloc refused",
                          (UINT32)order, 0U);
    }

    if (order == 0U) {
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], c);
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], b);
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], a);
    } else {
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], a);
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], b);
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], c);
    }

    if (!BoxMemSnapGet(&s1, "005 after")) {
        return 1U;
    }
    BOX_MEM_TRACE("[MEM] %s order=%u free=%u baseline=%u maxFree=%u\n",
                  caseName, order, s1.freeSize, s0.freeSize, s1.maxFree);
    if (!BoxMemSame(&s0, &s1)) {
        return BoxMemFail(caseName,
                          (order == 0U) ? "downward merge left residue"
                                        : "upward merge left residue",
                          s1.maxFree, s0.maxFree);
    }
    return 0U;
}

static UINT32 MemBox005Case(VOID)
{
    static const CHAR *caseName = "MEM-BOX-005";
    UINT32 fails = 0;
    UINT32 round;

    /* Three rounds of each order: a one-off merge bug often only shows after the
     * free list has been reshuffled once. */
    for (round = 0; round < 3U; round++) {
        fails += BoxMem005Round(0U, caseName);
        fails += BoxMem005Round(1U, caseName);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMem005(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMem005", MemBox005Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
