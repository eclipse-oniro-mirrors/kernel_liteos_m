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
 * BOX-B-002  Allocation contract: alloc / align / realloc / free  (boxB)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * The primitive surface, verified on boxB's own heap:
 *
 *   LOS_MemAlloc(pool, size)          costs ALIGN(size + 16, 4)
 *   LOS_MemAllocAlign(pool, size, b)  honours b and costs
 *                                     ALIGN(size + b - 4 + 16, 4)
 *   LOS_MemRealloc(pool, p, size)     grow keeps the payload, shrink keeps the
 *                                     head, ptr==NULL allocates, size==0 frees
 *   LOS_MemFree(pool, p)              returns the block exactly once
 *
 * Every step is judged by MEASURED heap delta against the snapshot taken at
 * entry, so the case is independent of what earlier/later cases left behind.
 *
 * ---------------------------------------------------------------------------
 * WHY RUN THE SAME CONTRACT TWICE (boxA has it too)
 * ---------------------------------------------------------------------------
 * The allocator is compiled ONCE into the COMMON sections and shared by every
 * box, so a bug found here is the same bug - but the POOL differs, and the pool
 * is what carries the per-box lock (mux). A box whose pool lock is misconfigured
 * would fail here while boxA passes, which is exactly the cross-box defect this
 * suite exists to catch.
 *
 * ---------------------------------------------------------------------------
 * WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - cost != formula          accounting changed shape; recheck BoxBAllocCost()
 *   - alignment not honoured   the gap-word path is broken
 *   - content lost across grow the copy path dropped the payload
 *   - final != baseline        something was not returned
 * ========================================================================== */

#include "It_los_boxB_usr_util.h"

#define B002_SIZE    96U
#define B002_ALIGN   32U
#define B002_BIG     512U
#define B002_SEED    0x5AU
#define B002_BLOCKS  8U

static VOID *g_b002Blk[B002_BLOCKS];

/* Position-dependent pattern: a realloc that returns the right pointer with the
 * wrong contents is worse than one that returns NULL, so verify byte-wise. */
static VOID B002Fill(UINT8 *p, UINT32 len, UINT32 seed)
{
    UINT32 i;

    for (i = 0; i < len; i++) {
        p[i] = (UINT8)((seed + i) & 0xFFU);
    }
}

static BOOL B002Check(const UINT8 *p, UINT32 len, UINT32 seed)
{
    UINT32 i;

    for (i = 0; i < len; i++) {
        if (p[i] != (UINT8)((seed + i) & 0xFFU)) {
            return FALSE;
        }
    }
    return TRUE;
}

static UINT32 MemB002Case(VOID)
{
    static const CHAR *caseName = "BOX-B-002";
    BoxBSnap s0;
    BoxBSnap s1;
    VOID *pool;
    VOID *p = NULL;
    VOID *q = NULL;
    UINT32 cost;
    UINT32 delta;
    UINT32 fails = 0;
    UINT32 got = 0;
    UINT32 i;

    if (!BoxBSnapGet(&s0)) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    pool = (VOID *)s0.pool;
    (VOID)memset_s(g_b002Blk, sizeof(g_b002Blk), 0, sizeof(g_b002Blk));

    /* ---- 1) plain alloc: the documented cost ---- */
    cost = BoxBAllocCost(B002_SIZE);
    p = LOS_MemAlloc(pool, B002_SIZE);
    if (p == NULL) {
        fails += BoxBFail(caseName, "MemAlloc refused", B002_SIZE, cost);
        goto GUARDS;
    }
    if (BoxBSnapGet(&s1)) {
        delta = (s1.freeSize <= s0.freeSize) ? (s0.freeSize - s1.freeSize) : 0xFFFFFFFFU;
        if (delta != cost) {
            fails += BoxBFail(caseName, "MemAlloc cost != ALIGN(size+16,4)",
                              delta, cost);
        }
    } else {
        fails++;
    }
    (VOID)LOS_MemFree(pool, p);
    p = NULL;

    /* ---- 2) aligned alloc ---- */
    cost = BoxBAlignAllocCost(B002_BIG, B002_ALIGN);
    q = LOS_MemAllocAlign(pool, B002_BIG, B002_ALIGN);
    if (q == NULL) {
        fails += BoxBFail(caseName, "MemAllocAlign refused", B002_BIG, cost);
    } else {
        if (((UINTPTR)q & (UINTPTR)(B002_ALIGN - 1U)) != 0U) {
            fails += BoxBFail(caseName, "alignment not honoured",
                              (UINT32)(UINTPTR)q, B002_ALIGN);
        }
        if (BoxBSnapGet(&s1)) {
            delta = (s1.freeSize <= s0.freeSize) ?
                    (s0.freeSize - s1.freeSize) : 0xFFFFFFFFU;
            if (delta != cost) {
                fails += BoxBFail(caseName, "AllocAlign cost != formula",
                                  delta, cost);
            }
        } else {
            fails++;
        }
        (VOID)LOS_MemFree(pool, q);
        q = NULL;
    }

    /* ---- 3) realloc: grow keeps contents, shrink keeps the head ---- */
    p = LOS_MemAlloc(pool, B002_SIZE);
    if (p == NULL) {
        fails += BoxBFail(caseName, "alloc for realloc refused", B002_SIZE, 0U);
        goto GUARDS;
    }
    B002Fill((UINT8 *)p, B002_SIZE, B002_SEED);
    q = LOS_MemRealloc(pool, p, B002_BIG);
    if (q == NULL) {
        fails += BoxBFail(caseName, "grow realloc refused", B002_SIZE, B002_BIG);
        (VOID)LOS_MemFree(pool, p);
        p = NULL;
        goto GUARDS;
    }
    p = q;
    if (!B002Check((const UINT8 *)p, B002_SIZE, B002_SEED)) {
        fails += BoxBFail(caseName, "content lost across grow",
                          (UINT32)(UINTPTR)p, B002_SIZE);
    }
    /* usable to the full new size */
    B002Fill((UINT8 *)p, B002_BIG, B002_SEED);
    if (!B002Check((const UINT8 *)p, B002_BIG, B002_SEED)) {
        fails += BoxBFail(caseName, "grown block unusable to new size",
                          B002_BIG, B002_BIG);
    }
    q = LOS_MemRealloc(pool, p, B002_SIZE);
    if (q == NULL) {
        fails += BoxBFail(caseName, "shrink realloc refused", B002_BIG, B002_SIZE);
    } else {
        p = q;
        if (!B002Check((const UINT8 *)p, B002_SIZE, B002_SEED)) {
            fails += BoxBFail(caseName, "shrink lost the head bytes",
                              B002_SIZE, 0U);
        }
    }
    (VOID)LOS_MemFree(pool, p);
    p = NULL;

    /* ---- 4) realloc's two delegating paths ---- */
    p = LOS_MemRealloc(pool, NULL, B002_SIZE);          /* == alloc */
    if (p == NULL) {
        fails += BoxBFail(caseName, "Realloc(ptr=NULL) did not allocate",
                          B002_SIZE, 0U);
    } else {
        (VOID)LOS_MemRealloc(pool, p, 0U);              /* == free */
        p = NULL;
    }

GUARDS:
    /* ---- 5) documented argument guards ----
     * These must be refused BEFORE any bookkeeping; a guard that fires late
     * would still have split a node. */
    if (LOS_MemAlloc(pool, 0U) != NULL) {
        fails += BoxBFail(caseName, "MemAlloc(0) not NULL", 0U, 0U);
    }
    if (LOS_MemAlloc(NULL, B002_SIZE) != NULL) {
        fails += BoxBFail(caseName, "MemAlloc(NULL pool) not NULL", 0U, 0U);
    }
    if (LOS_MemAlloc(pool, BOXB_NODE_USED_FLAG) != NULL) {
        fails += BoxBFail(caseName, "MemAlloc(USED_FLAG size) not NULL",
                          BOXB_NODE_USED_FLAG, 0U);
    }
    if (LOS_MemFree(pool, NULL) == LOS_OK) {
        fails += BoxBFail(caseName, "MemFree(NULL ptr) accepted", 0U, LOS_NOK);
    }
    if (LOS_MemFree(NULL, p) == LOS_OK) {
        fails += BoxBFail(caseName, "MemFree(NULL pool) accepted", 0U, LOS_NOK);
    }

    /* ---- 6) several blocks, then release: no aliasing, full recovery ---- */
    for (i = 0; i < B002_BLOCKS; i++) {
        g_b002Blk[i] = LOS_MemAlloc(pool, B002_SIZE);
        if (g_b002Blk[i] == NULL) {
            break;
        }
        B002Fill((UINT8 *)g_b002Blk[i], B002_SIZE, B002_SEED + i);
        got++;
    }
    if (got != B002_BLOCKS) {
        fails += BoxBFail(caseName, "could not place all blocks",
                          got, B002_BLOCKS);
    } else {
        /* each block must keep its own pattern */
        for (i = 0; i < B002_BLOCKS; i++) {
            if (!B002Check((const UINT8 *)g_b002Blk[i], B002_SIZE,
                           B002_SEED + i)) {
                fails += BoxBFail(caseName, "block pattern corrupted", i, 0U);
            }
        }
    }
    for (i = 0; i < B002_BLOCKS; i++) {
        if (g_b002Blk[i] != NULL) {
            (VOID)LOS_MemFree(pool, g_b002Blk[i]);
            g_b002Blk[i] = NULL;
        }
    }

    /* ---- 7) back to baseline ---- */
    if (!BoxBSnapGet(&s1)) {
        fails += BoxBFail(caseName, "final snapshot refused", 0U, 1U);
    } else if (!BoxBSame(&s0, &s1)) {
        fails += BoxBFail(caseName, "allocator left residue",
                          s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxBUsr002(VOID)
{
    TEST_ADD_CASE("ItLosBoxBUsr002", MemB002Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
