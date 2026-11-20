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
 * MEM-BOX-007  Invalid parameters are rejected without side effects  (group A)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * A box application is untrusted code: it will call the allocator with zero
 * sizes, NULL pools, NULL pointers and non-power-of-two alignments. Every one of
 * those must be rejected before any bookkeeping happens - the danger is not the
 * error return, it is a partially applied operation that corrupts the pool.
 *
 * The case therefore throws the whole invalid matrix at the allocator and then
 * requires the pool snapshot to be UNCHANGED. That "no side effect" assertion is
 * the real content: an implementation could reject the call and still have
 * split a node on the way to noticing the problem.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * The expected return for every case is NULL (alloc) or LOS_NOK (free). The case
 * does not care WHICH error path fires, only that (a) the call fails and (b) the
 * pool is untouched afterwards. If the baseline moves, one of the guards is
 * missing or fires after doing work.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

static UINT32 MemBox007Case(VOID)
{
    static const CHAR *caseName = "MEM-BOX-007";
    BoxMemSnap s0;
    BoxMemSnap s1;
    UINT32 fails = 0;

    if (!BoxMemSnapGet(&s0, "007 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    /* ---- allocator argument guards ---- */
    if (LOS_MemAlloc((VOID *)g_boxLocalPool[0], 0) != NULL) {
        fails += BoxMemFail(caseName, "alloc(0) not NULL", 0U, 0U);
    }
    if (LOS_MemAlloc(NULL, 64U) != NULL) {
        fails += BoxMemFail(caseName, "alloc(NULL pool) not NULL", 0U, 0U);
    }
    /* Alignment must be a power of two and at least pointer-sized. */
    if (LOS_MemAllocAlign((VOID *)g_boxLocalPool[0], 64U, 3U) != NULL) {
        fails += BoxMemFail(caseName, "alloc-align(3) not NULL", 3U, 0U);
    }
    if (LOS_MemAllocAlign((VOID *)g_boxLocalPool[0], 64U, 0U) != NULL) {
        fails += BoxMemFail(caseName, "alloc-align(0) not NULL", 0U, 0U);
    }

    /* ---- free argument guards ---- */
    if (LOS_MemFree((VOID *)g_boxLocalPool[0], NULL) == LOS_OK) {
        fails += BoxMemFail(caseName, "free(pool,NULL) accepted", 0U, LOS_NOK);
    }
    if (LOS_MemFree(NULL, g_boxLocalPool) == LOS_OK) {
        fails += BoxMemFail(caseName, "free(NULL,p) accepted", 0U, LOS_NOK);
    }

    /* ---- the pool must be byte-identical ---- */
    if (!BoxMemSnapGet(&s1, "007 final")) {
        fails++;
    } else if (!BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "invalid calls changed the pool",
                            s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMem007(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMem007", MemBox007Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
