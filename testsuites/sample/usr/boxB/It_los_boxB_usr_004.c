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
 * BOX-B-004  The accounting getters must agree on boxB too  (boxB group)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * The five read-only queries each implement their own walk over the node list,
 * and they do NOT walk identically (see the boxA equivalent, API-MEM-003):
 *
 *     LOS_MemPoolSizeGet   -> poolInfo->poolSize         (no walk)
 *     LOS_MemInfoGet       -> walks  node <  END         (excludes sentinel)
 *     LOS_MemTotalUsedGet  -> walks  node <= END         (INCLUDES sentinel)
 *     LOS_MemUsedBlksGet   -> walks  node <  END
 *     LOS_MemFreeBlksGet   -> walks  node <= END         (INCLUDES sentinel)
 *
 * and LosMemInfoGet additionally SEEDS totalUsedSize with
 * sizeof(LosMemPoolInfo) + OS_MULTI_DLNK_HEAD_SIZE, which LOS_MemTotalUsedGet
 * does not. So TotalUsedGet is SMALLER than MemInfoGet's total, not larger.
 *
 * Running it on boxB is not redundant with boxA: the numbers must be consistent
 * for THIS pool size (~12K vs boxA's ~7K), which exercises different bucket
 * boundaries in the free lists. A getter that hard-codes an assumption about
 * pool size would pass on boxA and fail here.
 *
 * ---------------------------------------------------------------------------
 * WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - used+free+sentinel != poolSize   bytes lost or double counted
 *   - UsedBlksGet != usedNodeNum       the two walks disagree on used nodes
 *   - TotalUsedGet > totalUsedSize     the seed difference was inverted: some
 *                                      change made TotalUsedGet count the header
 *   - used blocks did not grow by N    a block was allocated but not counted
 * ========================================================================== */

#include "It_los_boxB_usr_util.h"

#define B004_BLOCKS  6U
#define B004_SIZE    64U

static VOID *g_b004Blk[B004_BLOCKS];

static UINT32 MemB004Case(VOID)
{
    static const CHAR *caseName = "BOX-B-004";
    BoxBSnap s0;
    LOS_MEM_POOL_STATUS st = {0};
    VOID *pool;
    UINT32 poolSize;
    UINT32 usedBlks;
    UINT32 freeBlks;
    UINT32 usedTotal;
    UINT32 usedBlksAfter;
    UINT32 fails = 0;
    UINT32 got = 0;
    UINT32 i;

    if (!BoxBSnapGet(&s0)) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    pool = (VOID *)s0.pool;
    (VOID)memset_s(g_b004Blk, sizeof(g_b004Blk), 0, sizeof(g_b004Blk));

    poolSize  = LOS_MemPoolSizeGet((const VOID *)pool);
    usedTotal = LOS_MemTotalUsedGet(pool);
    usedBlks  = LOS_MemUsedBlksGet(pool);
    freeBlks  = LOS_MemFreeBlksGet(pool);

    if (LOS_MemInfoGet(pool, &st) != LOS_OK) {
        fails += BoxBFail(caseName, "MemInfoGet refused", 0U, LOS_OK);
        ICUNIT_ASSERT_EQUAL(fails, 0, fails);
        return LOS_OK;
    }

    dprintf("[BOXB] %s: poolSize=%u used=%u free=%u usedBlks=%u freeBlks=%u "
            "TotalUsedGet=%u\n", caseName, poolSize, st.totalUsedSize,
            st.totalFreeSize, usedBlks, freeBlks, usedTotal);

    /* ---- (a) the identity MemInfoGet must satisfy ---- */
    if ((st.totalUsedSize + st.totalFreeSize + BOXB_SENTINEL_SIZE) != poolSize) {
        fails += BoxBFail(caseName, "(a) used+free+sentinel != poolSize",
                          st.totalUsedSize + st.totalFreeSize + BOXB_SENTINEL_SIZE,
                          poolSize);
    }
    /* ---- (b) the two used-node walks must agree ---- */
    if (usedBlks != st.usedNodeNum) {
        fails += BoxBFail(caseName, "(b) UsedBlksGet != usedNodeNum",
                          usedBlks, st.usedNodeNum);
    }
    /* ---- (c)/(d) the `<=` walks can only be >=, and TotalUsedGet must stay
     * BELOW MemInfoGet's total because only the latter carries the seed ---- */
    if (freeBlks < st.freeNodeNum) {
        fails += BoxBFail(caseName, "(c) FreeBlksGet < freeNodeNum",
                          freeBlks, st.freeNodeNum);
    }
    if (usedTotal > st.totalUsedSize) {
        fails += BoxBFail(caseName, "(d) TotalUsedGet > totalUsedSize "
                          "(it has no seed, so it must be smaller)",
                          usedTotal, st.totalUsedSize);
    }

    /* ---- (e) the getters must track real allocations ---- */
    for (i = 0; i < B004_BLOCKS; i++) {
        g_b004Blk[i] = LOS_MemAlloc(pool, B004_SIZE);
        if (g_b004Blk[i] == NULL) {
            break;
        }
        got++;
    }
    if (got != B004_BLOCKS) {
        fails += BoxBFail(caseName, "could not place all blocks",
                          got, B004_BLOCKS);
    } else {
        usedBlksAfter = LOS_MemUsedBlksGet(pool);
        if (usedBlksAfter != (usedBlks + B004_BLOCKS)) {
            fails += BoxBFail(caseName, "(e) used blocks did not grow by N",
                              usedBlksAfter, usedBlks + B004_BLOCKS);
        }
    }

    /* ---- (f) everything back, every getter back to baseline ---- */
    for (i = 0; i < B004_BLOCKS; i++) {
        if (g_b004Blk[i] != NULL) {
            (VOID)LOS_MemFree(pool, g_b004Blk[i]);
            g_b004Blk[i] = NULL;
        }
    }
    if (LOS_MemUsedBlksGet(pool) != usedBlks) {
        fails += BoxBFail(caseName, "(f) used blocks did not return",
                          LOS_MemUsedBlksGet(pool), usedBlks);
    }
    if (LOS_MemTotalUsedGet(pool) != usedTotal) {
        fails += BoxBFail(caseName, "(f) TotalUsedGet did not return",
                          LOS_MemTotalUsedGet(pool), usedTotal);
    }
    if (LOS_MemFreeBlksGet(pool) != freeBlks) {
        fails += BoxBFail(caseName, "(f) FreeBlksGet did not return",
                          LOS_MemFreeBlksGet(pool), freeBlks);
    }
    if (LOS_MemPoolSizeGet((const VOID *)pool) != poolSize) {
        fails += BoxBFail(caseName, "(f) pool size changed",
                          LOS_MemPoolSizeGet((const VOID *)pool), poolSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxBUsr004(VOID)
{
    TEST_ADD_CASE("ItLosBoxBUsr004", MemB004Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
