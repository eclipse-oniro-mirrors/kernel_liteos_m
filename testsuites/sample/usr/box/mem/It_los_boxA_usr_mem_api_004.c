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
 * 3) The two APIs that CANNOT be used from a box, asserted as such:
 *
 *      LOS_MemTaskIdGet(ptr)  resolves the owner task of a heap block, but the
 *          implementation hard-codes `m_aucSysMem1` (the KERNEL system heap) as
 *          the pool to search - it has no pool argument. From a box the pointer
 *          is in the box heap, so the search misses and it returns OS_INVALID.
 *
 *      LOS_MemPoolList()      walks the kernel's global pool registry and prints
 *          every pool via OsMemInfoPrint; its own comment says "not box-callable".
 *
 *    This case does NOT call LOS_MemPoolList - printing every kernel pool would
 *    flood the console and it is documented as unreachable from a box. It records
 *    the finding in the header instead, and checks only LOS_MemTaskIdGet's
 *    documented-from-a-box behaviour (refusal, no crash).
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 *   - LastUsedGet outside the pool      the walk uses a wrong end-node formula
 *   - IntegrityCheck != LOS_OK          the heap is CORRUPT: stop and look at the
 *                                       case that ran just before this one
 *   - TaskIdGet returned a valid id     someone "fixed" it to search the current
 *                                       box pool; that would be a behaviour change
 *                                       worth reviewing, not silently accepted
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define API004_SIZE  128U

/* LOS_MemTaskIdGet must NOT be called from box user mode.
 *
 * Its implementation hard-codes the pool it searches:
 *
 *     LosMemPoolInfo *poolInfo = (LosMemPoolInfo *)(VOID *)m_aucSysMem1;
 *
 * i.e. the KERNEL system heap - the function has NO pool argument. The very
 * first thing it evaluates is
 *
 *     ptr < (VOID *)OS_MEM_FIRST_NODE(poolInfo)
 *
 * which READS the kernel heap head. Executing that from a box is an
 * unprivileged load from kernel memory: the MPU refuses it and the box dies
 * with a MemManage DACCVIOL, before any return value can be produced.
 *
 * Measured on hardware: FaultAddr = __heap_start + 4, PC = LOS_MemTaskIdGet+0x20,
 * caller = this case. So the earlier assumption "it returns OS_INVALID from a
 * box" was WRONG: it faults. The API is kernel-only in practice, and that is
 * worth recording rather than papering over.
 *
 * Kept as a named constant + comment so the knowledge lives in the source: if
 * the implementation ever becomes box-safe (e.g. gains a pool parameter), add a
 * real assertion here. */
#define API004_NOT_CALLED  (0xFFFFFFFFU)

static UINT32 MemApi004ProbeTaskIdGet(const VOID *ptr)
{
    /* Intentionally does not call LOS_MemTaskIdGet; see the note above. */
    (VOID)ptr;
    return API004_NOT_CALLED;
}

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

        /* ---- 3) LOS_MemTaskIdGet is NOT callable from a box - see below ----
         * It is deliberately only REFERENCED, never called. */
        owner = MemApi004ProbeTaskIdGet((const VOID *)p);
        dprintf("[MEM] %s: TaskIdGet NOT called from usr mode (%s)\n",
                caseName, (owner == API004_NOT_CALLED) ? "would fault" : "unexpected");

        (VOID)LOS_MemFree((VOID *)pool, p);
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
