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
 * BOX-B-003  Destroy and rebuild boxB's OWN heap   (boxB group)  <-- the point
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * A box heap is not garbage collected and not defragmented: it is REFORMATTED.
 * LOS_MemDeInit drops the pool from this box's registry and LOS_MemInit builds
 * a pristine free list again. That property is what makes a box restart a
 * reliable recovery mechanism - no matter how badly the application fragmented
 * or leaked, one restart yields a clean pool.
 *
 * This case is the only place in the whole test tree that can prove it, because
 * boxB has no other consumer: boxA's heap is shared by 30 cases, so destroying
 * it there would poison every later measurement.
 *
 *   LOS_MemDeInit(pool)          -> pool leaves the box registry
 *   LOS_MemInit(pool, size)      -> registry rejects an overlapping range, so a
 *                                   success here PROVES the DeInit really
 *                                   unlinked it
 *   alloc through the new pool   -> it must be a working pool, not just an
 *                                   accepted call
 *
 * ---------------------------------------------------------------------------
 * THE KNOWN DEFECT THIS CASE MUST WORK AROUND - AND REPORTS
 * ---------------------------------------------------------------------------
 * LOS_MemPoolInit creates a pool LOCK (MemLockCreate -> LOS_MuxCreate under BOX)
 * and LOS_MemDeInit NEVER DELETES IT. Every Init/DeInit cycle therefore leaks
 * one mux slot from this box's pool.
 *
 * boxB has LOSCFG_BOXB_MUX_LIMIT = 1 and the heap lock already owns it, so boxB
 * has ZERO spare mux slots. Consequences measured on hardware:
 *
 *   - the FIRST LOS_MemInit in the entry task succeeds (it consumes the only
 *     slot), so the pool exists and is usable;
 *   - LOS_MemDeInit succeeds (it only unlinks);
 *   - re-Init needs a NEW lock and there is none -> it fails with LOS_NOK.
 *
 * The case therefore asserts the sequence that is sound and records the one
 * that is not:
 *
 *   phase 1  the heap built by the entry task works            (must pass)
 *   phase 2  DeInit removes it: the pool is no longer usable   (must pass)
 *   phase 3  re-Init is EXPECTED TO FAIL on boxB because of the leak; a SUCCESS
 *            is reported as a note (it would mean the leak was fixed), not a
 *            failure, and the case re-initialises through the entry-style path
 *            only if that succeeded
 *
 * Written this way the case is a REGRESSION TEST FOR THE LEAK: if someone adds
 * the missing MemLockDelete, phase 3 starts succeeding and the note disappears.
 *
 * ---------------------------------------------------------------------------
 * WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - phase 1 fails   the heap the entry task built is broken
 *   - phase 2 fails   DeInit did not unlink: Init would then reject the range
 *   - "rebuild lost the whole pool"  a re-Init reported success but the pool is
 *                     not usable - worse than a clean refusal
 * ========================================================================== */

#include "It_los_boxB_usr_util.h"

#include "boxes_conf.h"

#define B003_BLK   128U

static UINT32 MemB003Case(VOID)
{
    static const CHAR *caseName = "BOX-B-003";
    BoxBSnap s0;
    VOID *pool;
    VOID *p = NULL;
    UINT32 ret;
    UINT32 fails = 0;

    if (!BoxBSnapGet(&s0)) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    pool = (VOID *)s0.pool;

    /* ---- phase 1: the heap the entry task built must work ---- */
    p = LOS_MemAlloc(pool, B003_BLK);
    if (p == NULL) {
        fails += BoxBFail(caseName, "phase1: boxB heap not usable",
                          B003_BLK, 0U);
        /* Nothing below is meaningful if the heap never worked. */
        ICUNIT_ASSERT_EQUAL(fails, 0, fails);
        return LOS_OK;
    }
    (VOID)LOS_MemFree(pool, p);
    p = NULL;
    dprintf("[BOXB] %s: phase1 OK (entry-built heap at 0x%x, size %u)\n",
            caseName, (UINT32)s0.pool, s0.poolSize);

    /* ---- phase 2: DeInit must unlink the pool ----
     * The proof is indirect but solid: OsMemMulPoolInit rejects any range that
     * overlaps a registered pool, so Init-after-DeInit only succeeds if DeInit
     * really removed the entry. That is asserted in phase 3. */
    ret = LOS_MemDeInit(pool);
    if (ret != LOS_OK) {
        fails += BoxBFail(caseName, "phase2: MemDeInit refused its own pool",
                          ret, LOS_OK);
        ICUNIT_ASSERT_EQUAL(fails, 0, fails);
        return LOS_OK;
    }
    dprintf("[BOXB] %s: phase2 OK (MemDeInit unlinked the pool)\n", caseName);

    /* ==================================================================
     * phase 3: THE REGRESSION TEST FOR THE MemDeInit POOL-LOCK LEAK
     * ==================================================================
     *
     * WHY THIS IS THE RIGHT ASSERTION
     * ------------------------------
     * LOS_MemInit walks this order:
     *
     *     MemLockCreate(pool, FALSE)  -> LOS_MuxCreate   <-- takes a mux slot
     *         on failure: return LOS_NOK, OsMemMulPoolInit is NOT reached
     *     MemLock / OsMemMulPoolInit / OsMemInit ...
     *
     * boxB has LOSCFG_BOXB_MUX_LIMIT = 1 and its heap lock already owns that
     * one slot. LOS_MemDeInit does MEM_LOCK / MEM_UNLOCK but never
     * MemLockDelete, so the slot from phase 1 is never returned. Therefore:
     *
     *     leak present -> the LOS_MuxCreate above FAILS -> Init returns LOS_NOK
     *     leak fixed   -> the slot is free again          -> Init SUCCEEDS
     *
     * So "re-Init refused" is not an expected-behaviour note, it is the
     * FAILURE this case exists to report. An earlier revision of this case
     * printed exactly that message and then passed anyway, which made a real
     * kernel defect look like a covered code path - the reason the assertion
     * below is explicit about the expected value instead of merely tolerating
     * a non-LOS_OK result.
     *
     * WHY IT IS SAFE TO LET THIS FAIL IN A BOX
     * ----------------------------------------
     * The only PRINT_ERR on this path lives in OsMemMulPoolInit ("address
     * overlaps"), and MemLockCreate returns BEFORE that call, so a failure here
     * never reaches a kernel-flash print from user mode. The returned value is
     * LOS_NOK (0xffffffff) specifically because it comes from MemLockCreate;
     * any other error would indicate a DIFFERENT failure and is reported as
     * such rather than being counted as evidence of this leak.
     *
     * ORDERING
     * --------
     * phase 2 already destroyed the heap, so if the leak is present boxB ends
     * this case with no heap. That is why the suite runs this case LAST.
     */
    ret = LOS_MemInit(pool, (UINT32)BOXB_HEAP_SIZE);
    if (ret == LOS_NOK) {
        /* The signature of the leak: MemLockCreate could not get a mux slot. */
        dprintf("[BOXB] %s: DEFECT CONFIRMED - MemDeInit leaked the pool lock.\n",
                caseName);
        dprintf("[BOXB] %s:   LOS_MemInit -> MemLockCreate -> LOS_MuxCreate failed,\n",
                caseName);
        dprintf("[BOXB] %s:   so Init returned LOS_NOK (0x%x). boxB has "
                "MUX_LIMIT=1 and\n", caseName, ret);
        dprintf("[BOXB] %s:   the lock created in phase 1 was never released by\n",
                caseName);
        dprintf("[BOXB] %s:   LOS_MemDeInit (MEM_LOCK/MEM_UNLOCK only, no "
                "MemLockDelete).\n", caseName);
        dprintf("[BOXB] %s: FIX: call MemLockDelete(pool) in LOS_MemDeInit "
                "after a successful\n", caseName);
        dprintf("[BOXB] %s:   OsMemMulPoolDeinit; this case then turns green.\n",
                caseName);
        fails += BoxBFail(caseName,
                          "MemDeInit leaked the pool lock (re-Init refused)",
                          ret, LOS_OK);
        ICUNIT_ASSERT_EQUAL(fails, 0, fails);
        return LOS_OK;
    }
    if (ret != LOS_OK) {
        /* Some other failure - do NOT attribute it to the lock leak. */
        dprintf("[BOXB] %s: phase3: re-Init failed with 0x%x, which is NOT "
                "LOS_NOK -\n", caseName, ret);
        dprintf("[BOXB] %s:   this is a different fault (not the mux-slot "
                "exhaustion),\n", caseName);
        dprintf("[BOXB] %s:   investigate separately.\n", caseName);
        fails += BoxBFail(caseName, "phase3: re-Init failed for an unexpected reason",
                          ret, LOS_OK);
        ICUNIT_ASSERT_EQUAL(fails, 0, fails);
        return LOS_OK;
    }

    /* Rebuild succeeded - the leak is fixed. Verify the pool is genuinely
     * usable rather than merely accepted. */
    dprintf("[BOXB] %s: phase3: re-Init SUCCEEDED - the MemDeInit pool-lock "
            "leak is FIXED\n", caseName);
    p = LOS_MemAlloc(pool, B003_BLK);
    if (p == NULL) {
        fails += BoxBFail(caseName, "phase3: rebuilt pool is not usable",
                          B003_BLK, 0U);
    } else {
        /* and it must be pristine: nothing used beyond the header seed */
        BoxBSnap s1;

        (VOID)LOS_MemFree(pool, p);
        p = NULL;
        if (BoxBSnapGet(&s1)) {
            if (s1.usedNodes != 0U) {
                fails += BoxBFail(caseName, "phase3: rebuilt pool is not pristine",
                                  s1.usedNodes, 0U);
            }
            if (s1.poolSize != (UINT32)BOXB_HEAP_SIZE) {
                fails += BoxBFail(caseName, "phase3: rebuilt pool has wrong size",
                                  s1.poolSize, (UINT32)BOXB_HEAP_SIZE);
            }
            dprintf("[BOXB] %s: phase3 rebuilt pool: size=%u used=%u free=%u\n",
                    caseName, s1.poolSize, s1.used, s1.freeSize);
        }
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxBUsr003(VOID)
{
    TEST_ADD_CASE("ItLosBoxBUsr003", MemB003Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
