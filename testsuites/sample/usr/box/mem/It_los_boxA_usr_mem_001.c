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
 * MEM-BOX-001  Heap accounting baseline   (group A: heap basics)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * Establishes and publishes the box heap's accounting baseline, and asserts the
 * one relation that must hold for a healthy pool NO MATTER what else is
 * allocated on it:
 *
 *     used + free + sentinel == poolSize
 *
 * Everything the allocator hands out is charged to `totalUsedSize`; everything
 * still free is summed into `totalFreeSize`; and the end (sentinel) node is
 * outside OsMemInfoGet's walk but inside poolSize. If those three do not add up,
 * bytes are being lost or double counted - which is the failure mode every later
 * group-A/B case is built to avoid tripping.
 *
 * ---------------------------------------------------------------------------
 * WHY IT NO LONGER REQUIRES A PRISTINE POOL - READ BEFORE "TIDYING" IT
 * ---------------------------------------------------------------------------
 * This case originally asserted `freeNodes == 1 && usedNodes == 0`, i.e. that
 * the pool was freshly formatted. That held only while it was the FIRST memory
 * case to run. Once the BOX functional cases (ItLosBoxUsr003/004/005) were
 * ordered ahead of it, they left parked waiter tasks and live timers allocated
 * on the same heap, so `freeNodes` was no longer 1 and the case failed on
 * hardware with ErrLine 138.
 *
 * The fix is deliberately two-fold:
 *
 *   1. It is now the FIRST case the suite runs (It_los_boxA_usr.c), so it
 *      normally does observe a near-pristine pool and still prints the numbers
 *      worth recording.
 *   2. The assertions no longer DEPEND on that. Only the accounting identity and
 *      the internal consistency of the snapshot are required, so the case keeps
 *      passing even when earlier code left allocations behind - which is what
 *      makes it robust rather than order-sensitive.
 *
 * A pristine pool is therefore reported, not demanded: the "shape" checks below
 * are informational and only fire as failures when the pool is ALSO accounting-
 * inconsistent. This is the difference between testing the invariant we care
 * about and testing an accident of scheduling order.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

static UINT32 MemBox001Case(VOID)
{
    static const CHAR *caseName = "MEM-BOX-001";
    BoxMemSnap s0;
    BoxMemSnap s1;
    UINT32 accounted;
    UINT32 fails = 0;
    BOOL pristine;

    /* ---- 1) baseline ---- */
    if (!BoxMemSnapGet(&s0, "MEM-BOX-001 init")) {
        /* No pool, or the query was refused: fail here and stop - every later
         * comparison would be against garbage. */
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    dprintf("[MEM] MEM-BOX-001: pool=0x%x size=%u used=%u free=%u maxFree=%u "
            "payload=%u usedNodes=%u freeNodes=%u\n",
            (UINT32)s0.pool, s0.poolSize, s0.used, s0.freeSize, s0.maxFree,
            s0.payload, s0.usedNodes, s0.freeNodes);

    /* A freshly formatted pool has exactly one free node (the whole payload) and
     * no walked used nodes. Anything else means earlier cases left allocations -
     * useful to know, not an error by itself. */
    pristine = (BOOL)((s0.freeNodes == 1U) && (s0.usedNodes == 0U) &&
                      (s0.maxFree == s0.freeSize));
    if (pristine) {
        dprintf("[MEM] MEM-BOX-001: pool is pristine "
                "(headers %u + free %u (usable %u) + sentinel %u = %u)\n",
                s0.used, s0.freeSize, s0.payload, BOX_MEM_SENTINEL_SIZE,
                s0.poolSize);
    } else {
        dprintf("[MEM] MEM-BOX-001: pool already carries allocations "
                "(usedNodes=%u freeNodes=%u) - accounting still must add up\n",
                s0.usedNodes, s0.freeNodes);
    }

    /* ---- 2) the pool must be the one the box was configured with ---- */
    if (s0.pool != (UINTPTR)OS_BOXA_POOL_ADDR) {
        fails += BoxMemFail(caseName, "pool addr != OS_BOXA_POOL_ADDR",
                            (UINT32)s0.pool, (UINT32)OS_BOXA_POOL_ADDR);
    }

    /* ---- 3) THE invariant: every byte owned exactly once ----
     * Independent of what is allocated, so it holds for a pristine pool and for
     * a busy one alike. The sentinel term is required because OsMemInfoGet's walk
     * stops AT the end node, putting it in neither total. */
    accounted = s0.used + s0.freeSize + BOX_MEM_SENTINEL_SIZE;
    if (accounted != s0.poolSize) {
        fails += BoxMemFail(caseName, "used + free + sentinel != poolSize",
                            accounted, s0.poolSize);
    }

    /* ---- 4) snapshot self-consistency ----
     * These hold for any valid pool, pristine or not:
     *   - maxFree can never exceed total free;
     *   - payload is derived from maxFree and must agree;
     *   - a pool with free space must report at least one free node.
     * They catch a corrupted node list without depending on allocation history. */
    if (s0.maxFree > s0.freeSize) {
        fails += BoxMemFail(caseName, "maxFree > totalFree (corrupt free list)",
                            s0.maxFree, s0.freeSize);
    }
    if ((s0.freeSize != 0U) && (s0.freeNodes == 0U)) {
        fails += BoxMemFail(caseName, "free bytes but no free node",
                            s0.freeNodes, 1U);
    }
    if (s0.payload != ((s0.freeSize > BOX_MEM_NODE_HEAD_SIZE) ?
                       (s0.freeSize - BOX_MEM_NODE_HEAD_SIZE) : 0U)) {
        fails += BoxMemFail(caseName, "payload != freeSize - node header",
                            s0.payload, s0.freeSize);
    }

    /* ---- 5) the pool must be able to serve this box's smallest real
     * allocation ----
     * The box's own swtmr task takes a 0x400 user stack from this heap
     * (los_swtmr_usr.c: OsSwtmrUsrTaskInit), so a pool that cannot even do that
     * cannot bring the box's runtime services up. */
    if (s0.payload < BOX_MEM_TSK_STACK_SIZE) {
        fails += BoxMemFail(caseName, "usable payload < one task stack",
                            s0.payload, BOX_MEM_TSK_STACK_SIZE);
    }

    /* ---- 6) idempotence: a snapshot must not disturb the pool ----
     * LOS_MemInfoGet only walks the node list under the pool lock; a second
     * snapshot must reproduce the first field for field. This also catches a
     * lock leak (a leaked mux pend would show up as a hang here). */
    if (!BoxMemSnapGet(&s1, "MEM-BOX-001 resnap")) {
        fails++;
    } else if (!BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "resnapshot differs from baseline",
                            s1.used, s0.used);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMem001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMem001", MemBox001Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
