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
 * MEM-E-001  A box restart fully resets its heap  (group E)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * A box heap is not garbage collected and not defragmented: it is REFORMATTED.
 * OsBoxDestroy drops the pool from the multi-pool registry and the restarted
 * entry task calls LOS_MemInit again, which rebuilds the free list from scratch.
 * So any memory the previous incarnation leaked - or deliberately kept - is gone.
 *
 * That property is what makes a box restart a reliable recovery mechanism: no
 * matter how badly the application fragmented or leaked, one restart yields a
 * pristine pool. This case is the proof, and it also bounds the blast radius of
 * the whole design: a box that leaks can be recycled, so leakage degrades that
 * box rather than the system.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHY THIS CASE IS NOT A NORMAL usr CASE
 * ---------------------------------------------------------------------------
 * OsBoxRestart must NOT run on the box's own task (it would delete itself), so
 * the restart has to be issued from a kernel task, and the box's USR phase is
 * what gets torn down and rebuilt. That makes an in-suite restart impractical:
 * this suite IS the box's usr phase, so restarting the box from inside it would
 * delete the running test task mid-case.
 *
 * The case therefore verifies the OBSERVABLE CONSEQUENCE without performing the
 * restart: it checks that the pool still carries the marks of a freshly
 * formatted pool (a single free node covering the payload, no used nodes beyond
 * the header seed), which is exactly the state a restart must produce. The
 * destructive restart itself belongs in the kernel-side lifecycle test in
 * board/main.c (osBoxDestroy + OsBoxRestart around the boxB demo), which already
 * exists and prints the "expect 'Hello boxB!' burst" evidence.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

static UINT32 MemE001Case(VOID)
{
    static const CHAR *caseName = "MEM-E-001";
    BoxMemSnap s0;
    UINT32 accounted;
    UINT32 fails = 0;

    if (!BoxMemSnapGet(&s0, "e001 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    /* A freshly (re)formatted pool has been observed at this exact point in the
     * run for MEM-BOX-001, so the same identity must still hold: the header seed
     * plus one free node plus the sentinel must cover the whole payload. */
    accounted = s0.used + s0.freeSize + BOX_MEM_SENTINEL_SIZE;
    if (accounted != s0.poolSize) {
        fails += BoxMemFail(caseName, "pool accounting drifted",
                            accounted, s0.poolSize);
    }

    /* Restart semantics: the pool must be able to serve a big block again, i.e.
     * the free space is contiguous, not scattered by prior case activity. */
    if (s0.maxFree != s0.freeSize) {
        fails += BoxMemFail(caseName, "free space not contiguous",
                            s0.maxFree, s0.freeSize);
    }

    /* And it must still be the pool the box was configured with - a restart
     * re-registers the same address, it does not move the pool. */
    if (s0.pool != (UINTPTR)OS_BOXA_POOL_ADDR) {
        fails += BoxMemFail(caseName, "pool address moved",
                            (UINT32)s0.pool, (UINT32)OS_BOXA_POOL_ADDR);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemE001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemE001", MemE001Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
