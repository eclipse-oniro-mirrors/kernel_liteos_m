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
 * API-MEM-001  LOS_MemAlloc / LOS_MemAllocAlign / LOS_MemFree   (group H)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * Covers the three primitive entry points of kernel/include/los_memory.h that
 * every other allocator call is built on, and pins down the CONTRACT each one
 * advertises in its header rather than only "it returns a pointer":
 *
 *   LOS_MemAlloc(pool, size)
 *     - refuses size 0 and NULL pool                     -> NULL
 *     - size with the USED or ALIGNED flag bit set        -> NULL
 *       (those bits live in the node header; passing them in a request is how a
 *        caller corrupts the free list, so the header rejects it)
 *     - otherwise costs ALIGN(size + 16, 4)
 *
 *   LOS_MemAllocAlign(pool, size, boundary)
 *     - the returned pointer is boundary-aligned
 *     - costs ALIGN(size + boundary - 4 + 16, 4): one extra word stores the gap
 *
 *   LOS_MemFree(pool, ptr)
 *     - NULL pool or NULL ptr                            -> LOS_NOK
 *     - a valid block is returned to the pool exactly once
 *
 * Every step is judged by MEASURED heap delta against the snapshot taken at
 * entry, so the case works whatever else the suite has allocated.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - "cost != formula"      the allocator's accounting changed shape; recheck
 *                            BoxMemAllocCost()/BoxMemAlignAllocCost() in
 *                            mem/It_los_boxA_usr_mem_util.h against los_memory.c
 *   - "refused <guard> not NULL"
 *                            an argument guard was dropped, so a bad request
 *                            reaches the split path
 *   - final snapshot != baseline
 *                            a block was not returned, or a freed block was
 *                            merged into the wrong hole
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define API001_SIZE      64U
#define API001_ALIGN     32U
#define API001_BIG       1024U

static UINT32 MemApi001Case(VOID)
{
    static const CHAR *caseName = "API-MEM-001";
    BoxMemSnap s0;
    BoxMemSnap s1;
    VOID *p = NULL;
    VOID *q = NULL;
    UINT32 cost;
    UINT32 delta;
    UINT32 fails = 0;

    if (!BoxMemSnapGet(&s0, "api001 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    /* ---- 1) LOS_MemAlloc: cost matches the documented formula ---- */
    cost = BoxMemAllocCost(API001_SIZE);
    p = LOS_MemAlloc((VOID *)g_boxLocalPool[0], API001_SIZE);
    if (p == NULL) {
        fails += BoxMemFail(caseName, "MemAlloc returned NULL", API001_SIZE, cost);
        goto GUARDS;
    }
    if (BoxMemSnapGet(&s1, "api001 alloc")) {
        delta = (s1.freeSize <= s0.freeSize) ? (s0.freeSize - s1.freeSize) : 0xFFFFFFFFU;
        if (delta != cost) {
            fails += BoxMemFail(caseName, "MemAlloc cost != ALIGN(size+16,4)", delta, cost);
        }
    } else {
        fails++;
    }
    if (LOS_MemFree((VOID *)g_boxLocalPool[0], p) != LOS_OK) {
        fails += BoxMemFail(caseName, "MemFree rejected its own block", 0U, LOS_OK);
    }
    p = NULL;
    if (!BoxMemSnapGet(&s1, "api001 freed") || !BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "heap not restored after free",
                            s1.freeSize, s0.freeSize);
    }

    /* ---- 2) LOS_MemAllocAlign: alignment honoured AND charged for ---- */
    cost = BoxMemAlignAllocCost(API001_BIG, API001_ALIGN);
    q = LOS_MemAllocAlign((VOID *)g_boxLocalPool[0], API001_BIG, API001_ALIGN);
    if (q == NULL) {
        fails += BoxMemFail(caseName, "MemAllocAlign returned NULL",
                            API001_BIG, cost);
    } else {
        if (((UINTPTR)q & (UINTPTR)(API001_ALIGN - 1U)) != 0U) {
            fails += BoxMemFail(caseName, "alignment not honoured",
                                (UINT32)(UINTPTR)q, API001_ALIGN);
        }
        if (BoxMemSnapGet(&s1, "api001 align")) {
            delta = (s1.freeSize <= s0.freeSize) ? (s0.freeSize - s1.freeSize) : 0xFFFFFFFFU;
            if (delta != cost) {
                fails += BoxMemFail(caseName, "AllocAlign cost != formula", delta, cost);
            }
        } else {
            fails++;
        }
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], q);
        q = NULL;
    }

GUARDS:
    /* ---- 3) the argument guards the header documents ----
     * These must be refused BEFORE any pool bookkeeping happens; a guard that
     * fires late would still have split a node. */
    if (LOS_MemAlloc((VOID *)g_boxLocalPool[0], 0U) != NULL) {
        fails += BoxMemFail(caseName, "MemAlloc(0) not NULL", 0U, 0U);
    }
    if (LOS_MemAlloc(NULL, API001_SIZE) != NULL) {
        fails += BoxMemFail(caseName, "MemAlloc(NULL pool) not NULL", 0U, 0U);
    }
    if (LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_NODE_USED_FLAG) != NULL) {
        fails += BoxMemFail(caseName, "MemAlloc(USED_FLAG size) not NULL",
                            BOX_MEM_NODE_USED_FLAG, 0U);
    }
    if (LOS_MemFree((VOID *)g_boxLocalPool[0], NULL) == LOS_OK) {
        fails += BoxMemFail(caseName, "MemFree(NULL ptr) accepted", 0U, LOS_NOK);
    }
    if (LOS_MemFree(NULL, p) == LOS_OK) {
        fails += BoxMemFail(caseName, "MemFree(NULL pool) accepted", 0U, LOS_NOK);
    }

    /* ---- 4) nothing above may have moved the heap ---- */
    if (!BoxMemSnapGet(&s1, "api001 final")) {
        fails++;
    } else if (!BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "guards changed the pool",
                            s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemApi001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemApi001", MemApi001Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
