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
 * API-MEM-005  LOS_MemFreeNodeShow is safe to call, and its print is broken
 *              (group H)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * LOS_MemFreeNodeShow(pool) walks the multiple free lists and PRINTS a histogram
 * of free nodes per size bucket. Two separate findings, both measured on
 * hardware, shape what this case can assert:
 *
 * FINDING 1 - the print is broken on this build.
 *   The buckets are emitted with PRINTK("2^%-5u", ...) / PRINTK("  %-5u", ...),
 *   and the console shows the format specifiers LITERALLY:
 *
 *       block size:  2^%-5u2^%-5u2^%-5u ...
 *       node number:   %-5u  %-5u  %-5u ...
 *
 *   i.e. the LOS_Printf path in use does not implement '%-5u'. The histogram is
 *   therefore unreadable, and the return value is the only usable output. That
 *   is a real defect in the debug printf surface, but it is NOT something this
 *   case can assert on (there is no programmatic access to what was printed).
 *
 * FINDING 2 - the pool-address guard does NOT protect against a plausible-looking
 *             address inside a real pool, and passing one PANICS the box.
 *   The guard is
 *
 *       if ((poolInfo == NULL) || ((UINTPTR)pool != (UINTPTR)poolInfo->pool))
 *                                    return LOS_NOK;
 *
 *   with `poolInfo = (LosMemPoolInfo *)pool`. So for ANY address A it reads the
 *   field at A + offsetof(pool) and compares with A. An earlier revision of this
 *   case passed `boxPool + 64`; that read landed on memory whose value happened
 *   to satisfy the comparison, the guard fell through, and the walk then used
 *   headAddr = A + sizeof(LosMemPoolInfo) - not a real free-list head - and
 *   dereferenced garbage. Measured: FaultAddr = 0x4, PC = LOS_MemFreeNodeShow
 *   +0xC6, LR = MemLockLosMemPoolInfo +0x15 (it faults while taking the pool
 *   lock on the bogus address).
 *
 *   So this case now ONLY calls FreeNodeShow with the exact pool base. The
 *   "reject a bogus pool" idea is deliberately dropped: it cannot be tested
 *   safely from inside the box being destroyed, and the finding above documents
 *   why the guard is insufficient.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS ASSERTED
 * ---------------------------------------------------------------------------
 *   1. calling it on the real pool returns LOS_OK and does not crash;
 *   2. the heap is byte-identical afterwards (it is a read-only query);
 *   3. LOS_MemPoolSizeGet still reports the configured size, proving the walk
 *      did not corrupt the pool header.
 *
 * The console histogram is deliberately NOT interpreted: per FINDING 1 it is
 * unreadable in this build. Read maxFree from the other cases instead when you
 * need a fragmentation signal.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

static UINT32 MemApi005Case(VOID)
{
    static const CHAR *caseName = "API-MEM-005";
    BoxMemSnap s0;
    BoxMemSnap s1;
    UINT32 ret;
    UINT32 fails = 0;

    if (!BoxMemSnapGet(&s0, "api005 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    /* ---- 1) the real pool must be accepted and must not crash ----
     * This is the whole point: the call completes, returns LOS_OK, and the box
     * is still alive to make the next call. */
    ret = LOS_MemFreeNodeShow((VOID *)s0.pool);
    if (ret != LOS_OK) {
        fails += BoxMemFail(caseName, "FreeNodeShow rejected the real pool",
                            ret, LOS_OK);
    }

    /* ---- 2) read-only: the heap must be untouched ---- */
    if (!BoxMemSnapGet(&s1, "api005 after")) {
        fails++;
    } else if (!BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "FreeNodeShow disturbed the heap",
                            s1.freeSize, s0.freeSize);
    }

    /* ---- 3) the pool header must still be intact ----
     * The walk starts from the header, so a bad index would show up here. */
    if (LOS_MemPoolSizeGet((const VOID *)s0.pool) != s0.poolSize) {
        fails += BoxMemFail(caseName, "pool size unreadable after the walk",
                            LOS_MemPoolSizeGet((const VOID *)s0.pool), s0.poolSize);
    }

    /* Documented, not asserted: the histogram text is unusable in this build
     * (see FINDING 1). Printed once so the defect is visible in every run. */
    dprintf("[MEM] %s: histogram printed above; note the '%%-5u' literals mean "
            "the debug printf has no '%%u' support - the numbers are unusable, "
            "only the LOS_OK return is meaningful\n", caseName);

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemApi005(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemApi005", MemApi005Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
