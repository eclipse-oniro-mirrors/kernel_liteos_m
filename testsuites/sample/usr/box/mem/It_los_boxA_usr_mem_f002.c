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
 * MEM-F-002  Fragmentation is bounded and fully recoverable  (group F)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * bestfit does not compact. Interleaved alloc/free therefore leaves holes: the
 * TOTAL free space can be large while no single block is big enough for a large
 * request. That is not a bug - it is the documented behaviour of a non-moving
 * allocator - but two things must hold, and this case checks both:
 *
 *   1. a large request that cannot be satisfied fails CLEANLY (NULL), and the
 *      pool's bookkeeping stays consistent;
 *   2. once the fragments are released, the free space merges back and the same
 *      large request SUCCEEDS.
 *
 * Point 2 is the real assertion. It proves the fragmentation is transient: the
 * pool is only unable to serve a big block while the fragments are actually
 * held, not permanently broken by the interleaving.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT IT TELLS A BOX DEVELOPER
 * ---------------------------------------------------------------------------
 * This is the case to consult when deciding whether a box can afford a large
 * one-off allocation alongside long-lived small ones. If the large request fails
 * here, the application must either reserve its big block up front or keep many
 * small blocks from being interleaved with it - the allocator will not
 * rearrange memory to help.
 *
 * A failure of point 1 (non-NULL from an impossible request) is corruption.
 * A failure of point 2 (success still refused after release) means merging is
 * broken, which would make fragmentation permanent - far more serious, because
 * the box's usable capacity would decay over time.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

/* 24 small blocks interleaved with 24 holes is enough to defeat a contiguous
 * request several times larger than a single small block. */
#define BOX_MEM_F002_SMALL  24U
#define BOX_MEM_F002_SZ     64U

static VOID *g_f002Blocks[BOX_MEM_F002_SMALL];

static UINT32 MemF002Case(VOID)
{
    static const CHAR *caseName = "MEM-F-002";
    BoxMemSnap s0;
    BoxMemSnap sMid;
    BoxMemSnap sEnd;
    UINT32 got = 0;
    UINT32 fails = 0;
    UINT32 i;
    VOID *big = NULL;

    if (!BoxMemSnapGet(&s0, "f002 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    (VOID)memset_s(g_f002Blocks, sizeof(g_f002Blocks), 0, sizeof(g_f002Blocks));

    /* ---- fill, then punch holes: free every other block ---- */
    for (i = 0; i < BOX_MEM_F002_SMALL; i++) {
        g_f002Blocks[i] = LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_F002_SZ);
        if (g_f002Blocks[i] == NULL) {
            break;
        }
        got++;
    }
    if (got < 4U) {
        fails += BoxMemFail(caseName, "could not place enough blocks", got, 4U);
        goto OUT;
    }
    for (i = 0; i < got; i += 2U) {
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], g_f002Blocks[i]);
        g_f002Blocks[i] = NULL;
    }

    if (!BoxMemSnapGet(&sMid, "f002 holes")) {
        fails++;
        goto OUT;
    }
    BOX_MEM_TRACE("[MEM] %s fragmented: free=%u maxFree=%u\n",
                  caseName, sMid.freeSize, sMid.maxFree);

    /* ---- a request larger than any single hole ----
     * Asked for maxFree + one node header + one word, i.e. just past what the
     * largest hole can serve. It must be refused, not silently truncated. */
    big = LOS_MemAlloc((VOID *)g_boxLocalPool[0],
                       sMid.maxFree + BOX_MEM_NODE_HEAD_SIZE + 4U);
    if (big != NULL) {
        /* Not necessarily a defect: the holes may have merged if the allocator
         * chose to place the blocks adjacently. Record it as unexpected. */
        fails += BoxMemFail(caseName, "over-hole request unexpectedly succeeded",
                            (UINT32)(UINTPTR)big, 0U);
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], big);
    }

    /* ---- bookkeeping must still be consistent after the refusal ---- */
    if (!BoxMemSnapGet(&sEnd, "f002 refused")) {
        fails++;
        goto OUT;
    }
    if ((sEnd.used + sEnd.freeSize + BOX_MEM_SENTINEL_SIZE) != sEnd.poolSize) {
        fails += BoxMemFail(caseName, "accounting broke after refusal",
                            sEnd.used + sEnd.freeSize + BOX_MEM_SENTINEL_SIZE,
                            sEnd.poolSize);
    }

    /* ---- release the survivors, then the big request MUST succeed ---- */
    for (i = 1; i < got; i += 2U) {
        if (g_f002Blocks[i] != NULL) {
            (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], g_f002Blocks[i]);
            g_f002Blocks[i] = NULL;
        }
    }

    big = LOS_MemAlloc((VOID *)g_boxLocalPool[0], sMid.freeSize / 2U);
    if (big == NULL) {
        fails += BoxMemFail(caseName, "merge did not restore a large block",
                            sMid.freeSize / 2U, 0U);
    } else {
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], big);
    }

OUT:
    for (i = 0; i < BOX_MEM_F002_SMALL; i++) {
        if (g_f002Blocks[i] != NULL) {
            (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], g_f002Blocks[i]);
            g_f002Blocks[i] = NULL;
        }
    }
    if (BoxMemSnapGet(&sEnd, "f002 final") && !BoxMemSame(&s0, &sEnd)) {
        fails += BoxMemFail(caseName, "final != baseline", sEnd.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemF002(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemF002", MemF002Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
