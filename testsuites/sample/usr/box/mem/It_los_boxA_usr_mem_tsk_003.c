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
 * MEM-TSK-003  What "forgetting to drain" actually costs   (group B)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * MEM-TSK-002 established that a dead task's user stack is only QUEUED, and
 * that the box must call LOS_TaskStackRecycle to get the bytes back. The
 * obvious follow-up question is: what happens if a box application simply never
 * calls it?
 *
 * The answer this case pins down is that the failure is DETERMINISTIC and
 * BENIGN, not silent corruption:
 *
 *   - every create/exit cycle WITHOUT a drain costs exactly one user stack
 *     (a staircase of `stackCost` bytes on the heap);
 *   - when the heap runs out, LOS_TaskCreate returns
 *     LOS_ERRNO_TSK_NO_MEMORY (an error code, not a crash);
 *   - a single drain at the end returns the whole staircase and the heap is
 *     back to its baseline - so nothing was destroyed, only withheld.
 *
 * This is the quantitative counterpart of MEM-TSK-002: 002 proves the two-stage
 * design, 003 measures the cost of ignoring stage two.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * Run it, then read the staircase from the log:
 *
 *   [MEM] MEM-TSK-003 round k: free=... (expected baseline - k*cost)
 *
 *   k grows until either the round budget (BOX_MEM_TSK_ROUNDS) or the heap is
 *   exhausted, whichever comes first. `survived` records how many rounds
 *   actually got a task created.
 *
 * The case PASSES when:
 *   - the staircase is exact for every round that created a task;
 *   - the first failure (if any) is a clean error code;
 *   - one final drain restores the baseline.
 *
 * It does NOT require the heap to be exhausted: on a generous heap the budget
 * ends first, which is also a pass. Set BOX_MEM_TSK_ROUNDS high (or shrink
 * BOXA_HEAP_SIZE) if you want to force the exhaustion path.
 *
 * ---------------------------------------------------------------------------
 * RELATION TO THE POOL-OVERFLOW RISK
 * ---------------------------------------------------------------------------
 * The queue that holds those un-drained addresses is `boxResRecycleStackList`,
 * sized `boxResTaskNum` (= LOSCFG_BOXA_TASK_LIMIT = 6). Its producer
 * (OsBoxUserStackRecycle) does:
 *
 *     if (boxResRecycleStackNum >= boxResTaskNum) LOS_Panic(...);
 *
 * So "create/exit without draining" has a SECOND limit before the heap one: the
 * queue. Which limit is hit first depends on the heap size divided by the stack
 * cost versus TASK_LIMIT. On this board the heap supports roughly
 * (10908 - a little) / 1044 ~= 9 un-drained stacks and the queue holds 6, so
 * the QUEUE is expected to hit first around round 6-7 - i.e. this case can
 * legitimately end in a panic. That is a REAL finding about the design, not a
 * test defect, and it is reported as such: see MEM-TSK-004's sibling note and
 * the group-C overflow case for the destructive variant.
 *
 * Because a panic would take the whole image down, the round budget here is
 * deliberately kept at BOX_MEM_TSK_ROUNDS (= 6), which stays AT OR BELOW
 * TASK_LIMIT so the normal regression run does not trip it. Raising the budget
 * is an explicit decision to probe the overflow.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

static volatile UINT32 g_t3Ran;
static volatile UINT32 g_t3Alive;

static VOID MemTsk003Entry(VOID)
{
    g_t3Ran++;
    g_t3Alive--;
}

static UINT32 MemTsk003Case(VOID)
{
    static const CHAR *caseName = "MEM-TSK-003";
    const UINT32 stackCost = BoxMemAlignAllocCost(BOX_MEM_TSK_STACK_SIZE,
                                                  (UINT32)LOSCFG_STACK_POINT_ALIGN_SIZE);
    BoxMemSnap s0;
    BoxMemSnap sNow;
    BoxMemSnap sFinal;
    UINT32 survived = 0;
    UINT32 exhausted = 0;
    UINT32 lastErr = LOS_OK;
    UINT32 taskId = 0;
    UINT32 ret;
    UINT32 fails = 0;
    UINT32 round;

    /* ---- 1) baseline ---- */
    if (!BoxMemSnapGet(&s0, "MEM-TSK-003 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }
    dprintf("[MEM] MEM-TSK-003: stack cost %u, rounds %u, task limit %u "
            "(queue holds %u un-drained stacks)\n",
            stackCost, BOX_MEM_TSK_ROUNDS, 6U, 6U);

    g_t3Ran = 0;
    g_t3Alive = 0;

    /* ---- 2) create/exit WITHOUT draining, round by round ---- */
    for (round = 0; round < BOX_MEM_TSK_ROUNDS; round++) {
        UINT32 before = g_t3Ran;

        g_t3Alive = 1;
        ret = BoxMemTskCreate((TSK_ENTRY_FUNC)MemTsk003Entry, "memTsk003",
                              BOX_MEM_TSK_STACK_SIZE, &taskId);
        if (ret != LOS_OK) {
            g_t3Alive = 0;
            exhausted = 1;
            lastErr = ret;
            dprintf("[MEM] MEM-TSK-003 round %u: create refused err 0x%x "
                    "(survived %u rounds) - clean failure, not a crash\n",
                    round, ret, survived);
            break;
        }
        survived++;

        /* Let it run and exit. No LOS_TaskStackRecycle here - that is the
         * whole point of the case. */
        (VOID)BoxMemTskWaitFlag(&g_t3Ran, before + 1U);
        (VOID)BoxMemTskWaitFlag(&g_t3Alive, 0);
        LOS_TaskDelay(BOX_MEM_TSK_SPIN_TICKS);

        if (!BoxMemSnapGet(&sNow, "MEM-TSK-003 round")) {
            fails++;
            break;
        }
        dprintf("[MEM] MEM-TSK-003 round %u: free=%u (expected %u), usedNodes=%u\n",
                round, sNow.freeSize, s0.freeSize - ((round + 1U) * stackCost),
                sNow.usedNodes);

        /* Staircase: each round must cost exactly one stack. A different step
         * means either the stack size is not what we think, or something else
         * is allocating/freeing behind our back. */
        if (sNow.freeSize != (s0.freeSize - ((round + 1U) * stackCost))) {
            fails += BoxMemFail(caseName, "staircase step != one stack cost",
                                sNow.freeSize,
                                s0.freeSize - ((round + 1U) * stackCost));
            break;
        }
    }

    dprintf("[MEM] MEM-TSK-003: %u rounds survived, exhausted=%u, lastErr=0x%x\n",
            survived, exhausted, lastErr);

    /* ---- 3) the failure, if any, must be a clean error code ---- */
    if (exhausted != 0) {
        if ((lastErr != LOS_ERRNO_TSK_NO_MEMORY) &&
            (lastErr != LOS_ERRNO_TSK_TCB_UNAVAILABLE)) {
            fails += BoxMemFail(caseName, "exhaustion gave an unexpected error code",
                                lastErr, LOS_ERRNO_TSK_NO_MEMORY);
        } else {
            dprintf("[MEM] MEM-TSK-003: exhaustion reported as expected "
                    "(NO_MEMORY = heap/stack pool, TCB_UNAVAILABLE = TCB pool)\n");
        }
    } else {
        dprintf("[MEM] MEM-TSK-003: heap not exhausted within %u rounds "
                "(raise BOX_MEM_TSK_ROUNDS to probe the limit)\n", BOX_MEM_TSK_ROUNDS);
    }

    /* ---- 4) one drain must return the WHOLE staircase ---- */
    if (!BoxMemTskSettle(&s0, &sFinal)) {
        fails += BoxMemFail(caseName, "single drain did not restore baseline",
                            sFinal.freeSize, s0.freeSize);
    }

    if (BoxMemSame(&s0, &sFinal)) {
        dprintf("[MEM] MEM-TSK-003: all withheld stacks returned by ONE drain "
                "- no permanent loss\n");
    } else {
        fails += BoxMemFail(caseName, "final snapshot != baseline",
                            sFinal.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemTsk003(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemTsk003", MemTsk003Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
