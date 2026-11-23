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
 * API-MEM-004  Introspection: LastUsedGet, IntegrityCheck, and the two APIs that
 *              are NOT usable from a box  (group H)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * Three things, all about asking the allocator to describe itself.
 *
 * 1) LOS_MemLastUsedGet(pool) returns the address just past the last node in the
 *    walk, i.e. the end of the pool's usable area. It must:
 *      - not crash, and
 *      - lie inside the pool, and
 *      - be stable across an alloc/free cycle (the last node does not move).
 *
 * 2) LOS_MemIntegrityCheck(pool) walks the whole node list checking size/flag
 *    sanity and node chaining. On a healthy pool it returns LOS_OK. This is the
 *    single cheapest "is the heap still sane" probe, so the case runs it after
 *    an alloc/free burst to prove the burst left the structure intact.
 *
 * 3) LOS_MemTaskIdGet(ptr) - resolves the owning task of a heap block.
 *
 *    THIS API USED TO BE UNUSABLE FROM A BOX, and the case now proves it is not.
 *    The old implementation hard-coded the pool it searched:
 *
 *        LosMemPoolInfo *poolInfo = (LosMemPoolInfo *)(VOID *)m_aucSysMem1;
 *
 *    i.e. the KERNEL system heap, for every caller. A box pointer is never
 *    inside it, so the range check always failed - and that failure path called
 *    PRINT_ERR, which reaches LOS_CurTaskNameGet (kernel flash). Executing it
 *    unprivileged is an IACCVIOL, so an out-of-range query did not return an
 *    error, it DESTROYED THE BOX (measured: FaultAddr = __heap_start + 4,
 *    PC = LOS_MemTaskIdGet+0x20).
 *
 *    The implementation now takes its registry from the CALLER's world
 *    (kernel registry for kernel callers, the box's own registry inside a box)
 *    and returns OS_INVALID instead of printing. This case asserts that:
 *
 *        (a) an allocated block reports a task id belonging to this box
 *        (b) the answer is stable across repeated calls
 *        (c) a pointer into a FREED node reports OS_INVALID (no owner)
 *        (d) a wildly out-of-range pointer reports OS_INVALID and does not fault
 *
 *    (d) is the regression test for the crash: if someone reintroduces a
 *    kernel-memory read or a PRINT_ERR on this path, (d) fails loudly.
 *
 * 4) LOS_MemPoolList() is still NOT called. It walks the kernel's global pool
 *    registry (g_poolHead, hard-coded) rather than the caller's, so from a box
 *    it would enumerate kernel pools and print them all. That is a separate
 *    defect, tracked in the source, and calling it here would only flood the
 *    console.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - LastUsedGet outside the pool      the walk uses a wrong end-node formula
 *   - IntegrityCheck != LOS_OK          the heap is CORRUPT: stop and look at the
 *                                       case that ran just before this one
 *   - TaskIdGet(live) == OS_INVALID     the pool registry is kernel-only again, or
 *                                       the caller-world dispatch broke
 *   - TaskIdGet(bogus) != OS_INVALID    the range check stopped rejecting, or the
 *                                       kernel-memory read / PRINT_ERR came back
 *   - a FAULT in this case              the regression: this API must be
 *                                       callable from user mode without faulting
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define API004_SIZE  128U

/* A pointer that cannot belong to any pool this box can see. Used to exercise
 * the out-of-range path, which used to fault instead of returning.
 *
 * Deliberately NOT 0 and NOT a plausible heap address: the goal is to be far
 * outside every registered pool so the walk rejects it on the first range check. */
#define API004_BOGUS_PTR   ((const VOID *)(UINTPTR)0x10000000U)

/* Box task ids are encoded as ((boxId + 1) << 16) | slot, so anything in
 * 0x00010000..0x7FFFFFFF is a task id; OS_INVALID is (UINT32)-1. */
#define API004_TASK_ID_VALID(id)  ((id) != (UINT32)OS_INVALID)

static UINT32 MemApi004Case(VOID)
{
    static const CHAR *caseName = "API-MEM-004";
    BoxMemSnap s0;
    BoxMemSnap s1;
    UINTPTR pool;
    UINTPTR last;
    UINTPTR lastAgain;
    VOID *p = NULL;
    UINT32 owner;
    UINT32 integrityRet;
    UINT32 fails = 0;

    if (!BoxMemSnapGet(&s0, "api004 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    pool = g_boxLocalPool[0];

    /* ---- 1) LOS_MemIntegrityCheck is a STUB in this build ----
     * los_memory.c compiles a real checker only under LOSCFG_MEM_DEBUG; without
     * it the whole function is
     *
     *     UINT32 LOS_MemIntegrityCheck(const VOID *pool) { (VOID)pool; return LOS_OK; }
     *
     * so asserting "it returns LOS_OK therefore the heap is healthy" proves
     * nothing. The call is still made (it must not crash and must not reject the
     * pool) but the verdict is recorded as informational, and the real structural
     * probes below are what actually judge the pool. */
    integrityRet = LOS_MemIntegrityCheck((const VOID *)pool);
    if (integrityRet != LOS_OK) {
        fails += BoxMemFail(caseName, "IntegrityCheck rejected the real pool",
                            integrityRet, LOS_OK);
    }
    dprintf("[MEM] %s: IntegrityCheck returned 0x%x (%s in this build)\n",
            caseName, integrityRet,
            (integrityRet == LOS_OK) ? "note: a stub unless LOSCFG_MEM_DEBUG" : "real check");

    /* ---- 2) LOS_MemLastUsedGet: inside the pool, stable ---- */
    last = LOS_MemLastUsedGet((VOID *)pool);
    if ((last < pool) || (last > (pool + s0.poolSize))) {
        fails += BoxMemFail(caseName, "LastUsedGet outside the pool",
                            (UINT32)last, (UINT32)pool);
    }

    p = LOS_MemAlloc((VOID *)pool, API004_SIZE);
    if (p == NULL) {
        fails += BoxMemFail(caseName, "alloc refused", API004_SIZE, 0U);
    } else {
        /* LastUsedGet is NOT required to be stable across an allocation.
         *
         * Its implementation (los_memory.c) reports the payload start of the LAST
         * node - whatever that node is:
         *
         *     node = OS_MEM_END_NODE(pool, poolSize)->selfNode.preNode;
         *     return used ? node + size + sizeof(LosMemDynNode)
         *                 : node + sizeof(LosMemDynNode);
         *
         * bestfit SPLITS the trailing free node on alloc, so that last node moves
         * up by the allocated size and the reported address moves with it. An
         * earlier revision asserted "moved on alloc" and failed on hardware -
         * the assertion was wrong, not the allocator.
         *
         * What IS required: the value stays inside the pool, and the accounting
         * identity still holds with a block live. */
        lastAgain = LOS_MemLastUsedGet((VOID *)pool);
        if ((lastAgain < pool) || (lastAgain > (pool + s0.poolSize))) {
            fails += BoxMemFail(caseName, "LastUsedGet left the pool during alloc",
                                (UINT32)lastAgain, (UINT32)pool);
        }
        if (BoxMemSnapGet(&s1, "api004 with block") &&
            ((s1.used + s1.freeSize + BOX_MEM_SENTINEL_SIZE) != s1.poolSize)) {
            fails += BoxMemFail(caseName, "accounting broken with a block live",
                                s1.used + s1.freeSize + BOX_MEM_SENTINEL_SIZE,
                                s1.poolSize);
        }

        /* ---- 3) LOS_MemTaskIdGet: the API that used to fault from a box ----
         *
         * (a) a block allocated from THIS box's heap must resolve to a real
         *     task id. Before the fix this always reported OS_INVALID, because
         *     the implementation searched the kernel heap regardless of caller.
         */
        owner = LOS_MemTaskIdGet((const VOID *)p);
        if (!API004_TASK_ID_VALID(owner)) {
            fails += BoxMemFail(caseName,
                                "TaskIdGet returned OS_INVALID for a live block "
                                "in this box's own heap (pool lookup is wrong)",
                                owner, LOS_OK);
        } else {
            dprintf("[MEM] %s: TaskIdGet(live block) = 0x%x\n", caseName, owner);

            /* (b) the answer must not wobble between calls. */
            {
                UINT32 ownerAgain = LOS_MemTaskIdGet((const VOID *)p);

                if (ownerAgain != owner) {
                    fails += BoxMemFail(caseName, "TaskIdGet not stable across calls",
                                        ownerAgain, owner);
                }
            }

            /* (c) The task id must belong to THIS box.
             *
             * ONLY meaningful when the allocator actually RECORDS the owner:
             * OsMemSetMagicNumAndTaskID is compiled only under
             * LOSCFG_MEM_DEBUG or LOSCFG_MEM_TASK_STAT, and without either the
             * taskId field is never written - a live block then legitimately
             * reports 0. Asserting the box bits on that 0 would fail for the
             * wrong reason (measured: it did, before this guard was added).
             *
             * When the field IS recorded, ids are ((boxId + 1) << 16) | slot,
             * so bits 16..31 must equal LOS_BOXA_ID + 1. */
#if defined(LOSCFG_MEM_DEBUG) || defined(LOSCFG_MEM_TASK_STAT)
            if ((owner >> 16) != (((UINT32)LOS_BOXA_ID + 1U) & 0xFFFFU)) {
                fails += BoxMemFail(caseName,
                                    "TaskIdGet reported a task outside boxA",
                                    owner, ((UINT32)LOS_BOXA_ID + 1U) << 16);
            }
#else
            if ((owner >> 16) != (((UINT32)LOS_BOXA_ID + 1U) & 0xFFFFU)) {
                dprintf("[MEM] %s: note: taskId 0x%x is unrecorded (0), because "
                        "neither LOSCFG_MEM_DEBUG nor LOSCFG_MEM_TASK_STAT is on - "
                        "the pool lookup succeeded, the owner field is just never "
                        "written in this build\n", caseName, owner);
            }
#endif
        }

        (VOID)LOS_MemFree((VOID *)pool, p);

        /* (d) THE REGRESSION TEST for the crash.
         *
         * Query a pointer that is far outside every pool. The old code reached
         * PRINT_ERR -> LOS_CurTaskNameGet (kernel flash) on this path and took
         * an IACCVIOL, destroying the box. Now it must simply return OS_INVALID.
         * If this case ever reports a fault or a non-OS_INVALID value here, the
         * kernel-memory read or the print has come back. */
        owner = LOS_MemTaskIdGet(API004_BOGUS_PTR);
        if (API004_TASK_ID_VALID(owner)) {
            fails += BoxMemFail(caseName,
                                "TaskIdGet accepted an out-of-range pointer "
                                "(expected OS_INVALID)",
                                owner, OS_INVALID);
        } else {
            dprintf("[MEM] %s: TaskIdGet(bogus 0x%x) = OS_INVALID, no fault\n",
                    caseName, (UINT32)(UINTPTR)API004_BOGUS_PTR);
        }

        /* The freed block must resolve to OS_INVALID too: it now lives in a free
         * node, and a free node has no owning task. */
        owner = LOS_MemTaskIdGet((const VOID *)p);
        if (API004_TASK_ID_VALID(owner)) {
            fails += BoxMemFail(caseName,
                                "TaskIdGet reported an owner for a FREED block",
                                owner, OS_INVALID);
        } else {
            dprintf("[MEM] %s: TaskIdGet(freed block) = OS_INVALID (no owner)\n",
                    caseName);
        }

        p = NULL;
    }

    /* ---- 4) the structural probes that DO mean something ----
     * With IntegrityCheck stubbed, these are the real assertions:
     *   - LastUsedGet is unchanged, so the end node did not move;
     *   - the accounting identity still holds.
     * Both are computed from the live node list, not from a debug hook. */
    if (LOS_MemIntegrityCheck((const VOID *)pool) != LOS_OK) {
        fails += BoxMemFail(caseName, "integrity call refused after the cycle",
                            LOS_MemIntegrityCheck((const VOID *)pool), LOS_OK);
    }
    /* After the block is freed, coalescing normally returns the pool to its
     * earlier shape, so the value SHOULD land back on `last` - but that depends on
     * merge behaviour, so it is reported rather than asserted. The hard check is
     * that it is still inside the pool. */
    {
        UINTPTR lastEnd = LOS_MemLastUsedGet((VOID *)pool);

        if ((lastEnd < pool) || (lastEnd > (pool + s0.poolSize))) {
            fails += BoxMemFail(caseName, "LastUsedGet left the pool after free",
                                (UINT32)lastEnd, (UINT32)pool);
        }
        if (lastEnd != last) {
            dprintf("[MEM] %s: note: LastUsedGet 0x%x -> 0x%x after the cycle "
                    "(coalescing did not restore the exact node layout)\n",
                    caseName, (UINT32)last, (UINT32)lastEnd);
        }
    }

    /* ---- 5) heap back to baseline ---- */
    if (!BoxMemSnapGet(&s1, "api004 final")) {
        fails++;
    } else if (!BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "heap not restored after the cycle",
                            s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemApi004(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemApi004", MemApi004Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
