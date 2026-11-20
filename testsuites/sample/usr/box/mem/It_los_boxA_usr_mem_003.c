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
 * MEM-BOX-003  Per-allocation node overhead, measured across sizes  (group A)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * Every allocation silently costs more than the caller asked for: the pool
 * prefixes each block with a control node and rounds the total up to the
 * alignment. The formula (los_memory.c OsMemAllocWithCheck) is
 *
 *     cost(N) = ALIGN(N + node header, OS_MEM_ALIGN_SIZE)
 *             = ALIGN(N + 16, 4)
 *
 * This case walks a table of request sizes and checks the MEASURED heap delta
 * against that formula for each one. The sizes are chosen so the ALIGN step is
 * actually visible: 1/4/16 sit inside one aligned word (20), 63/64 straddle a
 * boundary (80), 65 needs a new word (84), 1024 is already aligned (1040).
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * Run it once and read the numbers from the (verbose) trace if you need a cost
 * table for capacity planning; the verdict itself only asserts the formula. If
 * every size fails the same way, the formula constant (node header size or
 * alignment) is wrong; if only some fail, the split/merge path is suspect.
 *
 * The expected values are COMPUTED, never hard-coded, so a configuration change
 * (e.g. a different OS_MEM_ALIGN_SIZE) follows automatically.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

static const UINT32 g_sizes[] = { 1U, 4U, 16U, 63U, 64U, 65U, 100U, 1024U };

static UINT32 MemBox003Case(VOID)
{
    static const CHAR *caseName = "MEM-BOX-003";
    BoxMemSnap s0;
    BoxMemSnap s1;
    UINT32 fails = 0;
    UINT32 i;

    if (!BoxMemSnapGet(&s0, "003 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    for (i = 0; i < (UINT32)(sizeof(g_sizes) / sizeof(g_sizes[0])); i++) {
        const UINT32 cost = BoxMemAllocCost(g_sizes[i]);
        VOID *p = LOS_MemAlloc((VOID *)g_boxLocalPool[0], g_sizes[i]);

        if (p == NULL) {
            fails += BoxMemFail(caseName, "alloc refused", g_sizes[i], cost);
            continue;
        }
        if (BoxMemSnapGet(&s1, "003 alloc")) {
            UINT32 delta = (s1.freeSize <= s0.freeSize) ?
                           (s0.freeSize - s1.freeSize) : 0xFFFFFFFFU;
            BOX_MEM_TRACE("[MEM] %s size=%u delta=%u cost=%u\n",
                          caseName, g_sizes[i], delta, cost);
            if (delta != cost) {
                fails += BoxMemFail(caseName, "delta != ALIGN(size+16,4)", delta, cost);
            }
        } else {
            fails++;
        }
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], p);
    }

    /* Every block was freed: the pool must be back to the baseline. */
    if (!BoxMemSnapGet(&s1, "003 final") || !BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "sizes loop did not restore baseline",
                            s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMem003(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMem003", MemBox003Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
