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
 * API-MEM-002  LOS_MemRealloc: grow / shrink / ptr==NULL / size==0  (group H)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * LOS_MemRealloc is the one memory entry point with FOUR distinct paths, and
 * only two of them actually reallocate. The header/source define:
 *
 *     pool == NULL                     -> NULL               (refused)
 *     ptr  == NULL                     -> LOS_MemAlloc(size) (becomes an alloc)
 *     size == 0                        -> LOS_MemFree(ptr)   (becomes a free)
 *     otherwise                        -> grow in place if the next node is
 *                                         free, else copy + free the old block
 *
 * The two delegating paths are the dangerous ones: a caller that expects
 * "realloc never changes the pool size" is wrong for ptr==NULL, and a caller
 * that keeps using the pointer after size==0 is using freed memory. This case
 * pins all four behaviours so a refactor cannot quietly change them.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - "content lost across grow"   the copy path dropped the payload
 *   - "shrink lost the head bytes" the shrink path copied from the wrong offset
 *   - ptr==NULL did not allocate   the delegation to LOS_MemAlloc was removed
 *   - size==0 did not free         a leak on every "clear" call site
 *
 * Payload integrity is checked with a byte pattern before and after each step,
 * because a realloc that returns a valid pointer to the WRONG contents is worse
 * than one that returns NULL.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define API002_SMALL   32U
#define API002_BIG     256U
#define API002_SEED    0xA5U

/* Fill `len` bytes with a position-dependent pattern. */
static VOID Api002Fill(UINT8 *p, UINT32 len, UINT32 seed)
{
    UINT32 i;

    for (i = 0; i < len; i++) {
        p[i] = (UINT8)((seed + i) & 0xFFU);
    }
}

/* Verify the same pattern from offset 0 over `len` bytes. */
static BOOL Api002Check(const UINT8 *p, UINT32 len, UINT32 seed)
{
    UINT32 i;

    for (i = 0; i < len; i++) {
        if (p[i] != (UINT8)((seed + i) & 0xFFU)) {
            return FALSE;
        }
    }
    return TRUE;
}

static UINT32 MemApi002Case(VOID)
{
    static const CHAR *caseName = "API-MEM-002";
    BoxMemSnap s0;
    BoxMemSnap s1;
    VOID *p = NULL;
    VOID *q = NULL;
    UINT32 fails = 0;

    if (!BoxMemSnapGet(&s0, "api002 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    /* ---- 1) pool == NULL is refused ---- */
    if (LOS_MemRealloc(NULL, NULL, API002_SMALL) != NULL) {
        fails += BoxMemFail(caseName, "Realloc(NULL pool) not NULL", 0U, 0U);
    }

    /* ---- 2) ptr == NULL behaves as an allocation ---- */
    p = LOS_MemRealloc((VOID *)g_boxLocalPool[0], NULL, API002_BIG);
    if (p == NULL) {
        fails += BoxMemFail(caseName, "Realloc(ptr=NULL) did not allocate",
                            API002_BIG, 0U);
        goto FINAL;
    }
    if (!BoxMemSnapGet(&s1, "api002 realloc-alloc")) {
        fails++;
    } else if (s1.freeSize == s0.freeSize) {
        /* If it had NOT allocated, freeSize would be unchanged. */
        fails += BoxMemFail(caseName, "Realloc(ptr=NULL) charged nothing",
                            s1.freeSize, s0.freeSize);
    }

    /* ---- 3) grow: contents must survive ---- */
    Api002Fill((UINT8 *)p, API002_SMALL, API002_SEED);
    q = LOS_MemRealloc((VOID *)g_boxLocalPool[0], p, API002_BIG);
    if (q == NULL) {
        fails += BoxMemFail(caseName, "grow realloc returned NULL",
                            API002_SMALL, API002_BIG);
        p = NULL;
        goto FINAL;
    }
    if (!Api002Check((const UINT8 *)q, API002_SMALL, API002_SEED)) {
        fails += BoxMemFail(caseName, "content lost across grow",
                            (UINT32)(UINTPTR)q, API002_SMALL);
    }
    /* The grown block must be usable to its full new size. */
    Api002Fill((UINT8 *)q, API002_BIG, API002_SEED);
    if (!Api002Check((const UINT8 *)q, API002_BIG, API002_SEED)) {
        fails += BoxMemFail(caseName, "grown block unusable to new size",
                            API002_BIG, API002_BIG);
    }

    /* ---- 4) shrink: the head bytes must be preserved ---- */
    p = LOS_MemRealloc((VOID *)g_boxLocalPool[0], q, API002_SMALL);
    if (p == NULL) {
        fails += BoxMemFail(caseName, "shrink realloc returned NULL",
                            API002_BIG, API002_SMALL);
        q = NULL;
        goto FINAL;
    }
    q = NULL;
    if (!Api002Check((const UINT8 *)p, API002_SMALL, API002_SEED)) {
        fails += BoxMemFail(caseName, "shrink lost the head bytes",
                            API002_SMALL, 0U);
    }

    /* ---- 5) size == 0 behaves as a free ----
     * After this the pointer is invalid; the heap must show the block gone. */
    if (LOS_MemRealloc((VOID *)g_boxLocalPool[0], p, 0U) != NULL) {
        fails += BoxMemFail(caseName, "Realloc(size=0) not NULL", 0U, 0U);
    }
    p = NULL;

FINAL:
    /* Release anything still held so one failure cannot poison later cases. */
    if (p != NULL) {
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], p);
        p = NULL;
    }
    if (q != NULL) {
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], q);
        q = NULL;
    }

    if (!BoxMemSnapGet(&s1, "api002 final")) {
        fails++;
    } else if (!BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "realloc left residue",
                            s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemApi002(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemApi002", MemApi002Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
