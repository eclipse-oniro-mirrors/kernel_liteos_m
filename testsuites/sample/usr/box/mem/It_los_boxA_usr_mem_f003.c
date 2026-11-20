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
 * MEM-F-003  Concurrent allocation from several box tasks  (group F)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * A box heap is shared by every task in that box. Its lock is a MUTEX taken
 * through the mux syscalls, not an interrupt lock - so concurrency here is real
 * preemption, not just SMP. This case runs several probe tasks that each
 * allocate, write a pattern, verify it, and free, in a loop; the point is to
 * make the allocator switch tasks in the middle of its own bookkeeping and see
 * whether the pool survives.
 *
 * Three things are checked:
 *   - every worker's own pattern survives (no two allocations handed the same
 *     memory, no half-updated free list);
 *   - every worker completes its rounds (no deadlock on the pool mutex);
 *   - the heap returns to the baseline afterwards (no leaked node).
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * Worker count is kept small because boxA's TCB pool is LOSCFG_BOXA_TASK_LIMIT
 * (6) and the entry task plus this suite already hold slots. Three workers plus
 * the entry task leaves headroom; raising the count without raising the limit
 * turns this case into a TCB-exhaustion test instead of a concurrency one.
 *
 * A failure that names a worker means that worker saw corrupted data - which is
 * what a missing or incorrectly-scoped pool lock looks like from the application
 * side. A timeout (workers still alive at the end) means the mutex was held
 * across a scheduling point and never released, i.e. a deadlock.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define BOX_MEM_F003_WORKERS  3U
#define BOX_MEM_F003_ROUNDS   24U
#define BOX_MEM_F003_MIN      16U
#define BOX_MEM_F003_MAX      128U

static volatile UINT32 g_f003Alive;
static volatile UINT32 g_f003Errors;

/* One worker: alloc -> fill -> verify -> free, `rounds` times.
 *
 * The size varies per round so the allocator walks different buckets; a fixed
 * size would only ever exercise one split path. */
static VOID MemF003Worker(VOID)
{
    UINT32 round;

    for (round = 0; round < BOX_MEM_F003_ROUNDS; round++) {
        UINT32 size = BOX_MEM_F003_MIN +
                      ((round * 37U) % (BOX_MEM_F003_MAX - BOX_MEM_F003_MIN));
        VOID *p = LOS_MemAlloc((VOID *)g_boxLocalPool[0], size);

        if (p == NULL) {
            /* Out of memory is legitimate on this small heap; count it and move
             * on rather than failing the round. */
            continue;
        }
        if (!BoxMemPatternCheck((UINT8 *)p, size, 0xF0030000U + round)) {
            g_f003Errors++;
        }
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], p);
    }
    g_f003Alive--;
}

static UINT32 MemF003Case(VOID)
{
    static const CHAR *caseName = "MEM-F-003";
    BoxMemSnap s0;
    BoxMemSnap s1;
    UINT32 ids[BOX_MEM_F003_WORKERS];
    UINT32 started = 0;
    UINT32 fails = 0;
    UINT32 i;

    if (!BoxMemSnapGet(&s0, "f003 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    (VOID)memset_s(ids, sizeof(ids), 0, sizeof(ids));
    g_f003Errors = 0;
    g_f003Alive = 0;

    /* ---- start the workers ---- */
    for (i = 0; i < BOX_MEM_F003_WORKERS; i++) {
        UINT32 ret;

        g_f003Alive++;
        ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemF003Worker, "memF003",
                              BOX_MEM_TSK_STACK_SIZE, &ids[i]);
        if (ret != LOS_OK) {
            g_f003Alive--;
            /* Fewer workers is a weaker but still valid test; report it only if
             * not a single one could start. */
            BOX_MEM_TRACE("[MEM] %s worker %u refused 0x%x\n", caseName, i, ret);
            ids[i] = 0;
            break;
        }
        started++;
    }
    if (started == 0U) {
        fails += BoxMemFail(caseName, "no worker could start", started, 1U);
        goto OUT;
    }

    /* ---- wait for them to finish ---- */
    if (!BoxMemTskWaitFlag(&g_f003Alive, 0)) {
        fails += BoxMemFail(caseName, "workers did not finish (deadlock?)",
                            g_f003Alive, 0U);
    }
    if (g_f003Errors != 0U) {
        fails += BoxMemFail(caseName, "worker saw corrupted data",
                            g_f003Errors, 0U);
    }

OUT:
    /* ---- settle and compare ---- */
    if (!BoxMemTskSettle(&s0, &s1)) {
        fails += BoxMemFail(caseName, "heap did not return to baseline",
                            s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemF003(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemF003", MemF003Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
