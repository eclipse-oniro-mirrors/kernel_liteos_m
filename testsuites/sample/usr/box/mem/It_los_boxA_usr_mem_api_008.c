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
 * API-MEM-008  Per-module accounted allocation family         (group H)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * Covers the five module-accounted wrappers that los_memory.h exposes when
 * LOSCFG_MEM_MUL_MODULE is on. They are the plain allocator plus a module tag:
 *
 *   LOS_MemMalloc(pool, size, moduleId)          = MemAlloc + charge module
 *   LOS_MemMallocAlign(pool, size, bound, modId) = MemAllocAlign + charge module
 *   LOS_MemMfree(pool, ptr, moduleId)            = MemFree + uncharge module
 *   LOS_MemMrealloc(pool, ptr, size, moduleId)   = MemRealloc + re-tag
 *   LOS_MemMusedGet(moduleId)                    = total bytes charged to module
 *
 * The accounting is kept in a global array `g_moduleMemUsedSize[moduleId]`, and
 * each node stores its owner in the sizeAndFlag word (OS_MEM_MODID_SET). Two
 * consequences the case checks because they are easy to get wrong:
 *
 *   1. LOS_MemMfree COMPARES the caller's moduleId with the node's stored id, and
 *      on a mismatch PRINT_ERRs before using the node's own id. From a box that
 *      mismatch path is LETHAL: PRINT_ERR carries LOG_ERR_LEVEL, so
 *      OsLogLevelCheck calls LOS_CurTaskNameGet (KERNEL_FLASH) and the box dies
 *      with an IACCVIOL before the accounting is even touched. The case therefore
 *      never frees with a foreign id - see the note at that call site.
 *
 *   2. LOS_MemMusedGet is monotonic per module: it only moves when that module
 *      allocates or frees. The case therefore uses TWO module ids and proves
 *      that activity in one does not disturb the other's counter.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - module counter did not grow    the charge in MemMalloc was dropped
 *   - counter did not return to base the uncharge in MemMfree was dropped
 *   - the OTHER module's counter moved
 *                                    the module id is not being stored/read per
 *                                    node, so accounting is pool-global by
 *                                    accident
 *   - (a bogus moduleId cannot be probed here: its rejection path PRINT_ERRs and
 *      PRINT_ERR from user mode faults - see step 6 in the body)
 *
 * The box heap is snapshotted so the wrappers are also proven to be wrappers:
 * the underlying allocation must still be visible in the normal pool stats.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define API008_SIZE     96U
#define API008_ALIGN    16U
#define API008_MOD_A    1U      /* arbitrary but distinct ids          */
#define API008_MOD_B    2U

static UINT32 MemApi008Case(VOID)
{
    static const CHAR *caseName = "API-MEM-008";
    BoxMemSnap s0;
    BoxMemSnap s1;
    VOID *pool;
    VOID *a = NULL;
    VOID *b = NULL;
    UINT32 usedA0;
    UINT32 usedB0;
    UINT32 usedA1;
    UINT32 usedB1;
    UINT32 fails = 0;

    if (!BoxMemSnapGet(&s0, "api008 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    pool = (VOID *)s0.pool;

    /* The counters are GLOBAL and monotonic; another module (or a previous run of
     * this case) may already have charged them. Judge by DELTA, never absolute. */
    usedA0 = LOS_MemMusedGet(API008_MOD_A);
    usedB0 = LOS_MemMusedGet(API008_MOD_B);
    dprintf("[MEM] %s: module %u used=%u, module %u used=%u at entry\n",
            caseName, API008_MOD_A, usedA0, API008_MOD_B, usedB0);

    /* ---- 1) MemMalloc charges module A and nothing else ---- */
    a = LOS_MemMalloc(pool, API008_SIZE, API008_MOD_A);
    if (a == NULL) {
        fails += BoxMemFail(caseName, "MemMalloc refused", API008_SIZE, 0U);
        goto FINAL;
    }
    usedA1 = LOS_MemMusedGet(API008_MOD_A);
    usedB1 = LOS_MemMusedGet(API008_MOD_B);
    if (usedA1 <= usedA0) {
        fails += BoxMemFail(caseName, "module A counter did not grow",
                            usedA1, usedA0);
    }
    if (usedB1 != usedB0) {
        fails += BoxMemFail(caseName, "module B counter moved on an A alloc",
                            usedB1, usedB0);
    }
    /* The wrapper must be a wrapper: the plain pool accounting sees it too. */
    if (BoxMemSnapGet(&s1, "api008 allocated") &&
        (s1.used <= s0.used)) {
        fails += BoxMemFail(caseName, "MemMalloc did not touch the pool stats",
                            s1.used, s0.used);
    }

    /* ---- 2) MemMfree uncharges exactly what was charged ---- */
    if (LOS_MemMfree(pool, a, API008_MOD_A) != LOS_OK) {
        fails += BoxMemFail(caseName, "MemMfree refused its own block", 0U, LOS_OK);
        a = NULL;
        goto FINAL;
    }
    a = NULL;
    if (LOS_MemMusedGet(API008_MOD_A) != usedA0) {
        fails += BoxMemFail(caseName, "module A counter did not return",
                            LOS_MemMusedGet(API008_MOD_A), usedA0);
    }

    /* ---- 3) a SECOND block in A, freed with the CORRECT id ----
     * This used to free with the wrong moduleId on purpose. That cannot be done
     * from a box: LOS_MemMfree's mismatch branch is
     *
     *     if (moduleId != OS_MEM_MODID_GET(node)) {
     *         PRINT_ERR("node alloced in module %u, but free in module %u...");
     *         moduleId = OS_MEM_MODID_GET(node);        // then uses the NODE id
     *     }
     *
     * and PRINT_ERR is LOS_Printf(..., LOG_ERR_LEVEL, ...). That level is NOT
     * LOG_COMMON_LEVEL, so OsLogLevelCheck takes its level-prefix branch and calls
     * LOS_CurTaskNameGet - a KERNEL_FLASH function - from user mode. Measured:
     * MemManage IACCVIOL (Type 9), box destroyed. The box cannot observe the
     * mismatch behaviour at all, so the case asserts the sound part instead: a
     * correctly-tagged free returns the counter to exactly where it was, twice in
     * a row (no drift per cycle).
     */
    a = LOS_MemMalloc(pool, API008_SIZE, API008_MOD_A);
    if (a != NULL) {
        if (LOS_MemMusedGet(API008_MOD_A) <= usedA0) {
            fails += BoxMemFail(caseName, "second A alloc did not charge",
                                LOS_MemMusedGet(API008_MOD_A), usedA0);
        }
        (VOID)LOS_MemMfree(pool, a, API008_MOD_A);
        a = NULL;
        if (LOS_MemMusedGet(API008_MOD_A) != usedA0) {
            fails += BoxMemFail(caseName, "A counter drifted after cycle 2",
                                LOS_MemMusedGet(API008_MOD_A), usedA0);
        }
    }

    /* ---- 4) MemMallocAlign charges too, and still honours the boundary ---- */
    b = LOS_MemMallocAlign(pool, API008_SIZE, API008_ALIGN, API008_MOD_B);
    if (b == NULL) {
        fails += BoxMemFail(caseName, "MemMallocAlign refused",
                            API008_SIZE, 0U);
    } else {
        if (((UINTPTR)b & (UINTPTR)(API008_ALIGN - 1U)) != 0U) {
            fails += BoxMemFail(caseName, "MemMallocAlign lost the alignment",
                                (UINT32)(UINTPTR)b, API008_ALIGN);
        }
        if (LOS_MemMusedGet(API008_MOD_B) <= usedB0) {
            fails += BoxMemFail(caseName, "module B counter did not grow",
                                LOS_MemMusedGet(API008_MOD_B), usedB0);
        }
        if (LOS_MemMfree(pool, b, API008_MOD_B) != LOS_OK) {
            fails += BoxMemFail(caseName, "MemMfree(align block) refused",
                                0U, LOS_OK);
        }
        b = NULL;
    }

    /* ---- 5) MemMrealloc keeps a single charge for the block ---- */
    a = LOS_MemMalloc(pool, API008_SIZE, API008_MOD_A);
    if (a != NULL) {
        VOID *grown = LOS_MemMrealloc(pool, a, API008_SIZE * 2U, API008_MOD_A);
        if (grown == NULL) {
            fails += BoxMemFail(caseName, "MemMrealloc refused to grow",
                                API008_SIZE, API008_SIZE * 2U);
        } else {
            a = grown;
            /* Still exactly one block charged: freeing it must return to base. */
            if (LOS_MemMusedGet(API008_MOD_A) <= usedA0) {
                fails += BoxMemFail(caseName, "realloc lost the charge",
                                    LOS_MemMusedGet(API008_MOD_A), usedA0);
            }
        }
        (VOID)LOS_MemMfree(pool, a, API008_MOD_A);
        a = NULL;
        if (LOS_MemMusedGet(API008_MOD_A) != usedA0) {
            fails += BoxMemFail(caseName, "charge not balanced after realloc+free",
                                LOS_MemMusedGet(API008_MOD_A), usedA0);
        }
    }

    /* ---- 6) a bogus module id CANNOT be probed from a box - documented ----
     * The refusal path is
     *
     *     STATIC INLINE UINT32 OsMemModCheck(UINT32 moduleId)
     *     {
     *         if (moduleId > MEM_MODULE_MAX) {
     *             PRINT_ERR("error module ID input!\n");
     *             return LOS_NOK;
     *         }
     *         return LOS_OK;
     *     }
     *
     * and PRINT_ERR -> LOS_Printf(LOG_ERR_LEVEL) -> OsLogLevelCheck -> calls
     * LOS_CurTaskNameGet, which lives in KERNEL_FLASH. From user mode that is an
     * IACCVIOL and the box is destroyed before the refusal even returns.
     * Measured: Type 9, PC = LOS_CurTaskNameGet+0x0, LR = OsMemModCheck+0x12,
     * R5 = 0xFFFFFFFF (the id this case used to pass).
     *
     * So "invalid module id is rejected" is unobservable from a box. The case
     * documents it rather than executing it; the validation belongs in a
     * kernel-side test. Everything above already proves the happy path charges
     * and uncharges correctly.
     */

FINAL:
    if (a != NULL) {
        (VOID)LOS_MemMfree(pool, a, API008_MOD_A);
    }
    if (b != NULL) {
        (VOID)LOS_MemMfree(pool, b, API008_MOD_B);
    }

    /* ---- 7) the pool must be back to its entry state ---- */
    if (!BoxMemSnapGet(&s1, "api008 final")) {
        fails++;
    } else if (!BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "module wrappers left residue",
                            s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemApi008(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemApi008", MemApi008Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
