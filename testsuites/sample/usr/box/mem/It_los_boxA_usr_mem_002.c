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
 * MEM-BOX-002  One alloc/free pair must be exactly symmetric  (group A)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * The simplest possible heap invariant, and the template every later leak check
 * is built on: allocate a block, free it, and the pool must be byte-identical to
 * what it was before - not "roughly", but field for field (free bytes, node
 * counts, largest free node).
 *
 * It also pins the per-allocation overhead by measuring it rather than assuming
 * it: a request of N bytes costs ALIGN(N + node header, 4). The case checks the
 * MEASURED delta against the formula, so if the allocator's header size or
 * alignment ever changes, the expected cost follows automatically and only a
 * genuine mismatch fails.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 * Two distinct failures are possible and mean different things:
 *
 *   - "delta != cost"     the allocator's accounting changed (header size,
 *                         alignment, or the formula in the helper is stale);
 *   - "final != baseline" freeing did NOT return everything: a real leak, or a
 *                         merge bug that left a fragment behind.
 *
 * maxFree is compared as well, so a case where `freeSize` comes back but the
 * pool stayed fragmented is caught - that is the failure mode a free-bytes-only
 * check would miss.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

/* Request size chosen deliberately non-round: 100 is not a power of two and not
 * a multiple of the node header, so the ALIGN step is actually exercised. */
#define BOX_MEM_BOX002_SIZE  100U

static UINT32 MemBox002Case(VOID)
{
    static const CHAR *caseName = "MEM-BOX-002";
    const UINT32 cost = BoxMemAllocCost(BOX_MEM_BOX002_SIZE);
    BoxMemSnap s0;
    BoxMemSnap s1;
    BoxMemSnap s2;
    VOID *p;
    UINT32 delta;
    UINT32 fails = 0;

    if (!BoxMemSnapGet(&s0, "002 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    /* ---- allocate ---- */
    p = LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_BOX002_SIZE);
    if (p == NULL) {
        BOX_MEM_TRACE("[MEM] %s FAIL: alloc refused\n", caseName);
        ICUNIT_ASSERT_EQUAL(p != NULL, 1, 0);
        return LOS_OK;
    }

    if (!BoxMemSnapGet(&s1, "002 alloc")) {
        fails++;
    } else if (s1.freeSize > s0.freeSize) {
        fails += BoxMemFail(caseName, "free grew after alloc",
                            s1.freeSize, s0.freeSize);
    } else {
        delta = s0.freeSize - s1.freeSize;
        BOX_MEM_TRACE("[MEM] %s delta=%u cost=%u\n", caseName, delta, cost);
        if (delta != cost) {
            fails += BoxMemFail(caseName, "alloc delta != ALIGN(size+header,4)",
                                delta, cost);
        }
        if (s1.usedNodes != s0.usedNodes + 1U) {
            fails += BoxMemFail(caseName, "usedNodes did not grow by 1",
                                s1.usedNodes, s0.usedNodes + 1U);
        }
    }

    /* ---- free ---- */
    if (LOS_MemFree((VOID *)g_boxLocalPool[0], p) != LOS_OK) {
        BOX_MEM_TRACE("[MEM] %s FAIL: free refused\n", caseName);
        fails++;
    }

    if (!BoxMemSnapGet(&s2, "002 free")) {
        fails++;
    } else {
        BOX_MEM_TRACE("[MEM] %s after-free free=%u baseline=%u nodes=%u\n",
                      caseName, s2.freeSize, s0.freeSize, s2.freeNodes);
        if (!BoxMemSame(&s0, &s2)) {
            fails += BoxMemFail(caseName, "final != baseline (leak or fragment)",
                                s2.freeSize, s0.freeSize);
        }
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMem002(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMem002", MemBox002Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
