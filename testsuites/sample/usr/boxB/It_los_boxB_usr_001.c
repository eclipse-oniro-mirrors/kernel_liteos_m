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
 * BOX-B-001  boxB heap baseline and accounting identity   (boxB group)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * Establishes boxB's heap baseline and asserts the one relation that holds for
 * a healthy pool no matter what is allocated on it:
 *
 *     used + free + sentinel == poolSize
 *
 * It also records what makes boxB different from boxA: the pool is a SEPARATE
 * memory region (BOX_B_RAM, 0x2001C000..0x20020000) with its own size, so the
 * numbers here must match BOXB_HEAP_SIZE and must NOT match boxA's.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS IS WORTH ITS OWN CASE
 * ---------------------------------------------------------------------------
 * The whole point of a second box is isolation. If boxB's heap accidentally
 * aliased boxA's, or if the per-box pool registry (g_usrBoxPoolHead[]) handed
 * back the wrong pool, every later case in BOTH suites would be measuring the
 * wrong memory and would still "pass". This case pins the address, so that
 * failure mode cannot hide:
 *
 *   - pool == OS_BOXB_POOL_ADDR   (not boxA's, not the kernel heap)
 *   - poolSize == BOXB_HEAP_SIZE
 *   - the pool lies inside BOX_B_RAM
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - pool addr != OS_BOXB_POOL_ADDR
 *        the box is running against the wrong pool - check
 *        OsMemMulPoolHead()'s per-box dispatch and g_curBoxId
 *   - used + free + sentinel != poolSize
 *        bytes lost or double counted; the same identity the boxA suite uses
 *   - payload < 1024
 *        the heap cannot hold one 1K user stack, i.e. the box could not even
 *        spawn a task if its TASK_LIMIT allowed it
 * ========================================================================== */

#include "It_los_boxB_usr_util.h"

#include "boxes_conf.h"

static UINT32 MemB001Case(VOID)
{
    static const CHAR *caseName = "BOX-B-001";
    BoxBSnap s0;
    BoxBSnap s1;
    UINT32 accounted;
    UINT32 fails = 0;

    if (!BoxBSnapGet(&s0)) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    dprintf("[BOXB] %s: pool=0x%x size=%u used=%u free=%u maxFree=%u "
            "payload=%u usedNodes=%u freeNodes=%u\n",
            caseName, (UINT32)s0.pool, s0.poolSize, s0.used, s0.freeSize,
            s0.maxFree, s0.payload, s0.usedNodes, s0.freeNodes);

    /* ---- 1) it must be boxB's pool, not someone else's ---- */
    if (s0.pool != (UINTPTR)OS_BOXB_POOL_ADDR) {
        fails += BoxBFail(caseName, "pool != OS_BOXB_POOL_ADDR",
                          (UINT32)s0.pool, (UINT32)OS_BOXB_POOL_ADDR);
    }
    if (s0.poolSize != (UINT32)BOXB_HEAP_SIZE) {
        fails += BoxBFail(caseName, "poolSize != BOXB_HEAP_SIZE",
                          s0.poolSize, (UINT32)BOXB_HEAP_SIZE);
    }

    /* ---- 2) THE invariant ---- */
    accounted = s0.used + s0.freeSize + BOXB_SENTINEL_SIZE;
    if (accounted != s0.poolSize) {
        fails += BoxBFail(caseName, "used + free + sentinel != poolSize",
                          accounted, s0.poolSize);
    }

    /* ---- 3) snapshot self-consistency ---- */
    if (s0.maxFree > s0.freeSize) {
        fails += BoxBFail(caseName, "maxFree > totalFree (corrupt free list)",
                          s0.maxFree, s0.freeSize);
    }
    if ((s0.freeSize != 0U) && (s0.freeNodes == 0U)) {
        fails += BoxBFail(caseName, "free bytes but no free node",
                          s0.freeNodes, 1U);
    }
    if (s0.payload != ((s0.freeSize > BOXB_NODE_HEAD_SIZE) ?
                       (s0.freeSize - BOXB_NODE_HEAD_SIZE) : 0U)) {
        fails += BoxBFail(caseName, "payload != freeSize - node header",
                          s0.payload, s0.freeSize);
    }

    /* ---- 4) the heap must be able to hold one user stack ---- */
    if (s0.payload < 1024U) {
        fails += BoxBFail(caseName, "usable payload < one 1K stack",
                          s0.payload, 1024U);
    }

    /* ---- 5) taking a snapshot must not disturb the pool ---- */
    if (!BoxBSnapGet(&s1)) {
        fails += BoxBFail(caseName, "second snapshot refused", 0U, 1U);
    } else if (!BoxBSame(&s0, &s1)) {
        fails += BoxBFail(caseName, "resnapshot differs from baseline",
                          s1.used, s0.used);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxBUsr001(VOID)
{
    TEST_ADD_CASE("ItLosBoxBUsr001", MemB001Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
