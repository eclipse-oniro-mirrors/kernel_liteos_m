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
 * API-MEM-006  LOS_MemInit / LOS_MemPoolInit / LOS_MemDeInit on a box-local
 *              static array                                          (group H)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * The three pool-lifecycle entry points cannot be exercised on the box's own
 * heap: LOS_MemInit reformats it and LOS_MemDeInit destroys it, so every later
 * case in the suite would lose its pool. They do not need the box heap though -
 * they need ANY aligned block the caller owns. A static array inside the box is
 * exactly that, and it is the only way to cover these three from user mode.
 *
 *     LOS_MemInit(g_testPool, sizeof(g_testPool));   // format it
 *     ... alloc / free / query through po                     // use it
 *     LOS_MemDeInit(g_testPool);                     // tear it down
 *
 * ---------------------------------------------------------------------------
 * WHY THIS WORKS FROM USER MODE - THE PART THAT LOOKS IMPOSSIBLE
 * ---------------------------------------------------------------------------
 * LOS_MemInit is NOT a syscall here. The whole allocator lives once in the
 * shared COMMON sections, so every memory entry including LOS_MemInit is a
 * DIRECT call from the box world; only the pool LOCKS go through mux syscalls.
 *
 * The dangerous-looking part is the multipool registry: OsMemMulPoolInit walks
 * the registered pool list and dereferences each pool header, so a box doing
 * that against the KERNEL registry would read kernel RAM and fault. That is
 * why the registry is per-box:
 *
 *     VOID *g_usrBoxPoolHead[LOSCFG_BASE_BOX_MAX_LIMIT] = {0};   // R2, box-readable
 *
 *     STATIC INLINE VOID **OsMemMulPoolHead(VOID)
 *     {
 *         if (!LOS_IN_KERNEL && (g_curBoxId < LOSCFG_BASE_BOX_MAX_LIMIT)) {
 *             return &g_usrBoxPoolHead[g_curBoxId];   // this box's own list
 *         }
 *         return &g_poolHead;
 *     }
 *
 * Both dispatch inputs (g_sysMode, g_curBoxId) live in the R2 section the box
 * can read, so the walk stays inside box-accessible memory. A static array
 * therefore registers into THIS box's list, which is what makes the case legal.
 *
 * ---------------------------------------------------------------------------
 * WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - LOS_MemInit refused        size below OS_MEM_MIN_POOL_SIZE, or the array
 *                                was not 8-byte aligned
 *   - "overlaps" error           OsMemMulPoolInit found the new range colliding
 *                                with a pool already in this box's registry:
 *                                the array must not alias the box heap
 *   - alloc through the new pool
 *     returns NULL               OsMemInit did not format the block
 *   - LOS_MemDeInit refused      the pool was not in the registry, i.e. Init
 *                                did not complete
 *   - the box heap snapshot moves at all
 *                                these calls must not touch the box heap; the
 *                                array is separate static storage
 *
 * The box heap is snapshotted before and after and must be untouched: that is
 * the property that makes this case safe to run in the middle of the suite.
 *
 * ---------------------------------------------------------------------------
 * A DEFECT THIS CASE FOUND - DO NOT CALL MemInit TWICE ON A LIVE POOL
 * ---------------------------------------------------------------------------
 * OsMemMulPoolInit walks this box's registry and rejects a range overlapping an
 * already-registered pool. On that path it calls PRINT_ERR, and PRINT_ERR is
 *
 *     LOS_Printf(LOG_MODULE_KERNEL, LOG_ERR_LEVEL, ...)
 *
 * whose level is NOT LOG_COMMON_LEVEL. OsLogLevelCheck therefore takes the
 * level-prefix branch and calls LOS_CurTaskNameGet - a KERNEL_FLASH function -
 * from user mode. Measured: MemManage IACCVIOL (Type 9), PC = LOS_CurTaskNameGet
 * +0x0, LR = OsLogLevelCheck+0x15, box destroyed.
 *
 * So ANY error-path PRINT_* inside a COMMON-section function kills the box. The
 * comment at los_printf.c:62 assumes "PRINTK is always LOG_COMMON_LEVEL" and only
 * accounts for PRINTK, not PRINT_ERR/PRINT_WARN. That is a real kernel defect,
 * and the reason this case now avoids re-registering a live pool.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

/* Sized for the test, not for comfort.
 *
 * OS_MEM_MIN_POOL_SIZE is only 225 bytes (node head 16 + dlnk head 208 + 1), and
 * this case allocates a single 128-byte block, so 448 leaves plenty of room.
 * The number matters beyond this file: the array lives in .boxA.bss, which
 * shares the 16K BOX_A_RAM slot with the box heap - every byte here is a byte
 * the task cases cannot use for a 1044-byte user stack. An earlier 1024-byte
 * value shrank the heap from 8764 to 5676 and halved the task capacity.
 *
 * 8-byte aligned because LOS_MemPoolInit warns (and shrinks the pool) when the
 * base is not OS_MEM_ALIGN_SIZE aligned. */
#define API006_POOL_SIZE   448U

/* TWO blocks, because a pool may not be re-Registered while it is still in the
 * box registry: OsMemMulPoolInit walks the list and rejects an address range
 * that overlaps any pool already there. So LOS_MemPoolInit is exercised on a
 * SEPARATE block instead of on the one LOS_MemInit just registered. Each block
 * is 448 bytes; together they cost 896 of .boxA.bss, which shares the 16K
 * BOX_A_RAM slot with the box heap. */
/* TWO blocks, because a pool may not be re-Registered while it is still in the
 * box registry: OsMemMulPoolInit walks the list and rejects an address range that
 * overlaps any pool already there. LOS_MemPoolInit is therefore exercised on a
 * SEPARATE block, not on the one LOS_MemInit just registered. Each block is 448
 * bytes; together they cost 896 of .boxA.bss, which shares the 16K BOX_A_RAM
 * slot with the box heap. */
static UINT8 g_api006Pool[API006_POOL_SIZE] __attribute__((aligned(8)));
static UINT8 g_api006Pool2[API006_POOL_SIZE] __attribute__((aligned(8)));
static UINT8 g_api006Pool2[API006_POOL_SIZE] __attribute__((aligned(8)));

static UINT32 MemApi006Case(VOID)
{
    static const CHAR *caseName = "API-MEM-006";
    BoxMemSnap s0;
    BoxMemSnap s1;
    VOID *localPool = (VOID *)g_api006Pool;
    VOID *p = NULL;
    UINT32 ret;
    UINT32 fails = 0;
    UINT32 probe;
    UINT32 used;

    if (!BoxMemSnapGet(&s0, "api006 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    /* ---- 1) the array must not overlap the box heap ----
     * Both live in different sections, but check it: an overlap would make
     * LOS_MemInit fail the registry walk AND corrupt the real heap. */
    probe = (UINT32)(UINTPTR)g_api006Pool;
    if ((probe + API006_POOL_SIZE > (UINT32)s0.pool) &&
        (probe < (UINT32)s0.pool + s0.poolSize)) {
        fails += BoxMemFail(caseName, "static pool overlaps the box heap",
                            probe, (UINT32)s0.pool);
        ICUNIT_ASSERT_EQUAL(fails, 0, fails);
        return LOS_OK;
    }

    /* ---- 2) LOS_MemInit must accept the block ---- */
    ret = LOS_MemInit(localPool, API006_POOL_SIZE);
    if (ret != LOS_OK) {
        /* Nothing below is meaningful. */
        fails += BoxMemFail(caseName, "LOS_MemInit refused the static block",
                            ret, LOS_OK);
        ICUNIT_ASSERT_EQUAL(fails, 0, fails);
        return LOS_OK;
    }
    dprintf("[MEM] %s: LOS_MemInit OK on %u-byte static block at 0x%x\n",
            caseName, API006_POOL_SIZE, (UINT32)(UINTPTR)g_api006Pool);

    /* ---- 3) the freshly formatted pool must describe itself correctly ----
     * Same identity as MEM-BOX-001, but on a pool WE created: size is what we
     * asked for, and nothing is used beyond the header seed. */
    if (LOS_MemPoolSizeGet((const VOID *)localPool) != API006_POOL_SIZE) {
        fails += BoxMemFail(caseName, "new pool reports a different size",
                            LOS_MemPoolSizeGet((const VOID *)localPool),
                            API006_POOL_SIZE);
    }
    used = LOS_MemUsedBlksGet(localPool);
    if (used != 0U) {
        fails += BoxMemFail(caseName, "new pool already has used blocks",
                            used, 0U);
    }

    /* ---- 4) it must actually be usable ---- */
    p = LOS_MemAlloc(localPool, 128U);
    if (p == NULL) {
        fails += BoxMemFail(caseName, "alloc from the new pool failed",
                            128U, 0U);
    } else {
        /* The pointer must land INSIDE our array, not somewhere else. */
        if (((UINT32)(UINTPTR)p < probe) ||
            ((UINT32)(UINTPTR)p >= probe + API006_POOL_SIZE)) {
            fails += BoxMemFail(caseName, "alloc returned a pointer outside the block",
                                (UINT32)(UINTPTR)p, probe);
        }
        if (LOS_MemUsedBlksGet(localPool) != 1U) {
            fails += BoxMemFail(caseName, "used blocks did not become 1",
                                LOS_MemUsedBlksGet(localPool), 1U);
        }
        (VOID)LOS_MemFree(localPool, p);
        p = NULL;
        if (LOS_MemUsedBlksGet(localPool) != 0U) {
            fails += BoxMemFail(caseName, "used blocks did not return to 0",
                                LOS_MemUsedBlksGet(localPool), 0U);
        }
    }

    /* ---- 5) LOS_MemPoolInit on a SEPARATE block ----
     * The lower-level entry takes explicit attributes; LOS_MemInit is a thin
     * wrapper over it under LOSCFG_BOX (adding OS_MEM_LOCK_TYPE_MUX). It gets its
     * own block on purpose: the first block is STILL REGISTERED here, and
     * OsMemMulPoolInit rejects any range that overlaps a registered pool. Calling
     * MemPoolInit on `localPool` now would take the overlap error path, which in
     * turn calls PRINT_ERR - and that is a defect of its own (see the header
     * note about PRINT_ERR from user mode), so the case must not walk into it
     * by accident. */
    ret = LOS_MemPoolInit((VOID *)g_api006Pool2, API006_POOL_SIZE,
                          LOS_MEM_INIT_ATTR_SLAB_MSK);
    if (ret != LOS_OK) {
        fails += BoxMemFail(caseName, "LOS_MemPoolInit refused a free block",
                            ret, LOS_OK);
    } else {
        /* It must be a working pool, not just an accepted call. */
        VOID *p2 = LOS_MemAlloc((VOID *)g_api006Pool2, 64U);
        if (p2 == NULL) {
            fails += BoxMemFail(caseName, "MemPoolInit block is not usable",
                                64U, 0U);
        } else {
            (VOID)LOS_MemFree((VOID *)g_api006Pool2, p2);
        }
        if (LOS_MemDeInit((VOID *)g_api006Pool2) != LOS_OK) {
            fails += BoxMemFail(caseName, "MemDeInit refused the MemPoolInit pool",
                                0U, LOS_OK);
        }
    }

    /* ---- 6) LOS_MemDeInit must take the first block out of the registry ---- */
    ret = LOS_MemDeInit(localPool);
    if (ret != LOS_OK) {
        fails += BoxMemFail(caseName, "LOS_MemDeInit refused the pool it created",
                            ret, LOS_OK);
    }

    /* ---- 7) DeInit must have removed it from the registry ----
     *
     * The obvious proof is "Init again succeeds", and an earlier revision did
     * exactly that. It cannot be done here: LOS_MemPoolInit creates a pool LOCK
     * (MemLockCreate -> a MUX under BOX) and LOS_MemDeInit NEVER DELETES IT, so
     * every Init/DeInit cycle leaks one mux slot. boxA has LOSCFG_BOXA_MUX_LIMIT
     * = 4 and the heap lock already owns one, leaving 3 - a couple of extra
     * cycles starve the rest of the suite (measured: the condvar case then failed
     * with LOS_ERRNO_MUX_ALL_BUSY and C001/C006 could not create a single mux).
     *
     * That leak is a kernel defect, reported separately. This case therefore runs
     * exactly ONE Init/DeInit pair on each of its two blocks and leaves the
     * "re-register after DeInit" proof to a kernel-side test, where the mux pool
     * is not shared with 30 other cases. */
    dprintf("[MEM] %s: note: LOS_MemDeInit leaks the pool lock (MemLockCreate is "
            "never matched by MemLockDelete) - one mux slot per pool is gone\n",
            caseName);

    /* ---- 8) the BOX heap must never have been touched ---- */
    if (!BoxMemSnapGet(&s1, "api006 final")) {
        fails++;
    } else if (!BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "static-pool lifecycle moved the box heap",
                            s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemApi006(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemApi006", MemApi006Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
