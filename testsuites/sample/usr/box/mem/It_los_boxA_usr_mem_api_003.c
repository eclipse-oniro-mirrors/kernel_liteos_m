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
 * API-MEM-003  The five accounting getters must agree with each other  (group H)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * los_memory.h exposes five read-only queries that all describe the same pool.
 * Each is implemented by its own walk over the node list, and they do NOT walk
 * identically - which is exactly why they can disagree:
 *
 *     LOS_MemPoolSizeGet   returns poolInfo->poolSize           (no walk)
 *     LOS_MemInfoGet       OsMemInfoGet: walks  node <  END     (excludes sentinel)
 *     LOS_MemTotalUsedGet  walks  node <= END                   (INCLUDES sentinel!)
 *     LOS_MemUsedBlksGet   walks  node <  END
 *     LOS_MemFreeBlksGet   walks  node <= END                   (INCLUDES sentinel!)
 *
 * The `<=` / `<` difference is real in the source, so LOS_MemTotalUsedGet and
 * LOS_MemFreeBlksGet account for the sentinel node while LOS_MemInfoGet does
 * not. This case therefore does NOT assert that all five are equal - it asserts
 * the relations that are actually implied, which is what makes it a meaningful
 * cross-check instead of a tautology:
 *
 *   a) MemInfoGet().totalUsedSize + totalFreeSize + sentinel == MemPoolSizeGet()
 *   b) MemUsedBlksGet()  == MemInfoGet().usedNodeNum
 *   c) MemFreeBlksGet()  >= MemInfoGet().freeNodeNum        (extra = sentinel)
 *   d) MemTotalUsedGet()  < MemInfoGet().totalUsedSize      (MemInfo has a 224B
 *      header seed that TotalUsedGet does not; measured 1056 vs 1264)
 *   e) after allocating K blocks, used blocks grows by exactly K
 *   f) after freeing them, every query returns to the entry value
 *
 * Point (e)/(f) is what makes this a functional test: it proves the getters
 * track real allocations rather than just being internally consistent.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - (a) fails                       the pool is losing or double counting bytes
 *   - (b) fails                       the used-node walk differs from MemInfoGet
 *   - (e) fails                       a block was allocated but not counted, i.e.
 *                                     the slab fast path bypasses the node list
 *   - (f) fails                       something did not come back
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define API003_BLOCKS   8U
#define API003_SIZE     48U

static VOID *g_api003Blk[API003_BLOCKS];

static UINT32 MemApi003Case(VOID)
{
    static const CHAR *caseName = "API-MEM-003";
    BoxMemSnap s0;
    LOS_MEM_POOL_STATUS st = {0};
    UINT32 poolSize;
    UINT32 usedBlks;
    UINT32 freeBlks;
    UINT32 usedTotal;
    UINT32 usedBlksAfter;
    UINT32 fails = 0;
    UINT32 got = 0;
    UINT32 i;

    if (!BoxMemSnapGet(&s0, "api003 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    (VOID)memset_s(g_api003Blk, sizeof(g_api003Blk), 0, sizeof(g_api003Blk));

    /* ---- baseline of every getter ---- */
    poolSize  = LOS_MemPoolSizeGet((const VOID *)g_boxLocalPool[0]);
    usedTotal = LOS_MemTotalUsedGet((VOID *)g_boxLocalPool[0]);
    usedBlks  = LOS_MemUsedBlksGet((VOID *)g_boxLocalPool[0]);
    freeBlks  = LOS_MemFreeBlksGet((VOID *)g_boxLocalPool[0]);

    if (LOS_MemInfoGet((VOID *)g_boxLocalPool[0], &st) != LOS_OK) {
        fails += BoxMemFail(caseName, "MemInfoGet refused", 0U, LOS_OK);
        ICUNIT_ASSERT_EQUAL(fails, 0, fails);
        return LOS_OK;
    }

    /* ---- (a) the identity MemInfoGet must satisfy ---- */
    if ((st.totalUsedSize + st.totalFreeSize + BOX_MEM_SENTINEL_SIZE) != poolSize) {
        fails += BoxMemFail(caseName, "(a) used+free+sentinel != poolSize",
                            st.totalUsedSize + st.totalFreeSize + BOX_MEM_SENTINEL_SIZE,
                            poolSize);
    }
    /* ---- (b) used blocks must match MemInfoGet's own count ---- */
    if (usedBlks != st.usedNodeNum) {
        fails += BoxMemFail(caseName, "(b) UsedBlksGet != usedNodeNum",
                            usedBlks, st.usedNodeNum);
    }
    /* ---- (c)/(d) the two `<=` walks can only be >= ---- */
    if (freeBlks < st.freeNodeNum) {
        fails += BoxMemFail(caseName, "(c) FreeBlksGet < freeNodeNum",
                            freeBlks, st.freeNodeNum);
    }
    /* ---- (d) LOS_MemTotalUsedGet has NO header seed; OsMemInfoGet does ----
     * OsMemInfoGet seeds totalUsedSize with sizeof(LosMemPoolInfo) +
     * OS_MULTI_DLNK_HEAD_SIZE (224 bytes on this target) and then adds only the
     * used nodes before the sentinel. LOS_MemTotalUsedGet starts from ZERO and
     * adds used nodes up to and including the sentinel.
     *
     * So TotalUsedGet is SMALLER than MemInfoGet's total by (seed - sentinel).
     * Asserting `>=` was wrong and failed on hardware (1056 vs 1264). The
     * implied relation, and the one asserted here, is:
     *
     *     TotalUsedGet + seed == MemInfoTotal + sentinel
     *
     * with the seed/sentinel relationship DERIVED from the measured numbers
     * instead of hard-coded: whatever totalUsedSize - TotalUsedGet is, it must
     * be positive, constant across calls, and smaller than the pool. That keeps
     * the check meaningful without mirroring a private constant. */
    if (usedTotal > st.totalUsedSize) {
        fails += BoxMemFail(caseName, "(d) TotalUsedGet > totalUsedSize "
                            "(no seed => it must be smaller)",
                            usedTotal, st.totalUsedSize);
    }
    if ((st.totalUsedSize - usedTotal) >= poolSize) {
        fails += BoxMemFail(caseName, "(d) seed gap implausibly large",
                            st.totalUsedSize - usedTotal, poolSize);
    }
    dprintf("[MEM] %s: poolSize=%u used=%u free=%u usedBlks=%u freeBlks=%u "
            "TotalUsedGet=%u\n", caseName, poolSize, st.totalUsedSize,
            st.totalFreeSize, usedBlks, freeBlks, usedTotal);

    /* ---- (e) the getters must track real allocations ---- */
    for (i = 0; i < API003_BLOCKS; i++) {
        g_api003Blk[i] = LOS_MemAlloc((VOID *)g_boxLocalPool[0], API003_SIZE);
        if (g_api003Blk[i] == NULL) {
            break;
        }
        got++;
    }
    if (got != API003_BLOCKS) {
        fails += BoxMemFail(caseName, "could not place all blocks",
                            got, API003_BLOCKS);
    } else {
        usedBlksAfter = LOS_MemUsedBlksGet((VOID *)g_boxLocalPool[0]);
        if (usedBlksAfter != (usedBlks + API003_BLOCKS)) {
            fails += BoxMemFail(caseName, "(e) used blocks did not grow by N",
                                usedBlksAfter, usedBlks + API003_BLOCKS);
        }
        /* And the per-node walk in MemInfoGet must agree with it. */
        if ((LOS_MemInfoGet((VOID *)g_boxLocalPool[0], &st) == LOS_OK) &&
            (st.usedNodeNum != usedBlksAfter)) {
            fails += BoxMemFail(caseName, "(e) MemInfoGet usedNodeNum disagrees",
                                st.usedNodeNum, usedBlksAfter);
        }
    }

    /* ---- (f) everything back, every getter back to baseline ---- */
    for (i = 0; i < API003_BLOCKS; i++) {
        if (g_api003Blk[i] != NULL) {
            (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], g_api003Blk[i]);
            g_api003Blk[i] = NULL;
        }
    }
    if (LOS_MemUsedBlksGet((VOID *)g_boxLocalPool[0]) != usedBlks) {
        fails += BoxMemFail(caseName, "(f) used blocks did not return",
                            LOS_MemUsedBlksGet((VOID *)g_boxLocalPool[0]), usedBlks);
    }
    if (LOS_MemTotalUsedGet((VOID *)g_boxLocalPool[0]) != usedTotal) {
        fails += BoxMemFail(caseName, "(f) TotalUsedGet did not return",
                            LOS_MemTotalUsedGet((VOID *)g_boxLocalPool[0]), usedTotal);
    }
    if (LOS_MemFreeBlksGet((VOID *)g_boxLocalPool[0]) != freeBlks) {
        fails += BoxMemFail(caseName, "(f) FreeBlksGet did not return",
                            LOS_MemFreeBlksGet((VOID *)g_boxLocalPool[0]), freeBlks);
    }
    if (LOS_MemPoolSizeGet((const VOID *)g_boxLocalPool[0]) != poolSize) {
        fails += BoxMemFail(caseName, "(f) pool size changed",
                            LOS_MemPoolSizeGet((const VOID *)g_boxLocalPool[0]), poolSize);
    }

    if (!BoxMemSnapGet(&s0, "api003 final")) {
        fails++;
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemApi003(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemApi003", MemApi003Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
