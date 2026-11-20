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
 * MEM-BOX-004  Aligned-allocation overhead across boundaries  (group A)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * LOS_MemAllocAlign is the call the box TASK create path uses for the user
 * stack, so its overhead formula is what MEM-TSK-001 predicts the task cost
 * from. The extra work is a gap word stored just below the aligned pointer:
 *
 *     useSize = size + boundary - sizeof(gapSize)
 *     cost    = ALIGN(useSize + 16, 4)
 *
 * For size 1024 this gives boundary 4 -> 1040, boundary 8 -> 1044,
 * boundary 16 -> 1052. The middle value is exactly what one box task costs,
 * which is why this case and MEM-TSK-001 cross-check each other.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * If MEM-TSK-001's expected task cost ever disagrees with reality, run this
 * first: it isolates whether the ALIGNED formula is wrong, without any task
 * machinery in the way. A failure here means the cost model is stale; a pass
 * here with a failing MEM-TSK-001 means the task path allocates something else.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

static const UINT32 g_bounds[] = { 4U, 8U, 16U };
#define BOX_MEM_BOX004_SIZE  1024U

static UINT32 MemBox004Case(VOID)
{
    static const CHAR *caseName = "MEM-BOX-004";
    BoxMemSnap s0;
    BoxMemSnap s1;
    UINT32 fails = 0;
    UINT32 i;

    if (!BoxMemSnapGet(&s0, "004 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    for (i = 0; i < (UINT32)(sizeof(g_bounds) / sizeof(g_bounds[0])); i++) {
        const UINT32 bound = g_bounds[i];
        const UINT32 cost = BoxMemAlignAllocCost(BOX_MEM_BOX004_SIZE, bound);
        VOID *p = LOS_MemAllocAlign((VOID *)g_boxLocalPool[0], BOX_MEM_BOX004_SIZE, bound);

        if (p == NULL) {
            fails += BoxMemFail(caseName, "alloc-align refused", bound, cost);
            continue;
        }
        /* The returned pointer must honour the requested alignment - otherwise
         * the cost we just charged is meaningless. */
        if (((UINTPTR)p & (UINTPTR)(bound - 1U)) != 0U) {
            fails += BoxMemFail(caseName, "alignment not honoured",
                                (UINT32)(UINTPTR)p, bound);
        }
        if (BoxMemSnapGet(&s1, "004 alloc")) {
            UINT32 delta = (s1.freeSize <= s0.freeSize) ?
                           (s0.freeSize - s1.freeSize) : 0xFFFFFFFFU;
            BOX_MEM_TRACE("[MEM] %s bound=%u delta=%u cost=%u\n",
                          caseName, bound, delta, cost);
            if (delta != cost) {
                fails += BoxMemFail(caseName, "aligned delta != formula", delta, cost);
            }
        } else {
            fails++;
        }
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], p);
    }

    if (!BoxMemSnapGet(&s1, "004 final") || !BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "bounds loop did not restore baseline",
                            s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMem004(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMem004", MemBox004Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
