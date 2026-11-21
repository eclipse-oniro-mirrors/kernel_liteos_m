/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the following conditions:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ItLosEvent044 - LOS_EventCondWrite: first-ever cover of the conditional
 * write (an API implemented since the baseline with zero callers).
 *
 * CONTRACT (kernel/base/los_event.c, observed and pinned here): CondWrite
 * wakes EVERY waiter parked on the event REGARDLESS of each waiter's read
 * mask, writes NO event bits (uwEventID is untouched), and the woken
 * LOS_EventRead RETURNS to its caller - the caller is expected to
 * re-evaluate its own predicate (the refs LOS_EventCondRead pattern).
 * That is what separates it from LOS_EventWrite, which wakes only the
 * waiters whose masks match the written bits.
 *
 *  phase 1 - negative: NULL cb refused, uninitialized list refused
 *  phase 2 - two waiters with masks unsatisfied by the current bits:
 *     CondWrite releases BOTH (each read returns 0 - nothing matched -
 *     instead of re-parking to its timeout) and the event bits are
 *     still exactly what they were
 *  phase 3 - the contrast that defines the conditional: a plain write
 *     of NON-matching bits leaves a parked waiter parked (it times out);
 *     a plain write of matching bits completes it with those bits */

#include "osTest.h"
#include "It_los_event.h"

/* The body lives in kernel/base/los_event.c unconditionally, but the
 * declaration in los_event.h hides behind LOSCFG_COMPAT_POSIX (off here);
 * a test-local extern is the least invasive way to reach it. The name is
 * NOT in the box syscall rename list, so it binds the kernel body. */
extern UINT32 LOS_EventCondWrite(PEVENT_CB_S eventCB);

static EVENT_CB_S g_condEvent;

/* 0 = not started, 1 = parked, 2 = read returned */
static volatile UINT32 g_condState[2] = {0, 0};
static volatile UINT32 g_condWoke[2] = {0, 0};
static volatile UINT32 g_condRet[2] = {0, 0};

static VOID CondWaiterRun(UINT32 idx, UINT32 mask, UINT32 timeout)
{
    g_condState[idx] = 1;
    g_condRet[idx] = LOS_EventRead(&g_condEvent, mask, LOS_WAITMODE_OR, timeout);
    if (g_condRet[idx] != LOS_ERRNO_EVENT_READ_TIMEOUT) {
        g_condWoke[idx] = 1;
    }
    g_condState[idx] = 2;
}

static VOID TaskF044A(VOID) { CondWaiterRun(0, 0x11, 8); }
static VOID TaskF044B(VOID) { CondWaiterRun(1, 0x22, 8); }
/* phase 3a: waits on 0x11 while only non-matching bits ever arrive */
static VOID TaskF044C(VOID) { CondWaiterRun(0, 0x11, 8); }
/* phase 3b: same mask, matching bits arrive */
static VOID TaskF044D(VOID) { CondWaiterRun(0, 0x11, 40); }

static UINT32 SpawnWaiter(TSK_ENTRY_FUNC entry, CHAR *name, UINT32 *taskId)
{
    TSK_INIT_PARAM_S task = {0};

    task.pfnTaskEntry = entry;
    task.uwStackSize = TASK_STACK_SIZE_TEST;
    task.pcName = name;
    task.usTaskPrio = TASK_PRIO_TEST - 2;
    task.uwResved = LOS_TASK_ATTR_JOINABLE;
    return LOS_TaskCreate(taskId, &task);
}

/* Park waiters [0, n): poll instead of a fixed sleep (fixed delays race
 * against equal-priority load - the M6O lesson). */
static UINT32 WaitParked(UINT32 n)
{
    UINT32 i;
    UINT32 spin;

    for (spin = 0; spin < 20; spin++) {
        for (i = 0; (i < n) && (g_condState[i] == 1); i++) {
        }
        if (i == n) {
            break;
        }
        LOS_TaskDelay(2);
    }
    for (i = 0; i < n; i++) {
        if (g_condState[i] != 1) {
            return 1;
        }
    }
    return 0;
}

static UINT32 Testcase(VOID)
{
    UINT32 waitA = OS_INVALID;
    UINT32 waitB = OS_INVALID;
    UINT32 fails = 0;
    UINT32 ret;

    /* ---- phase 1: negative paths ---- */
    ret = LOS_EventCondWrite(NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_EVENT_PTR_NULL, ret);

    {
        EVENT_CB_S uninit;
        (VOID)memset_s(&uninit, sizeof(EVENT_CB_S), 0, sizeof(EVENT_CB_S));
        ret = LOS_EventCondWrite(&uninit);
        ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_EVENT_NOT_INITIALIZED, ret);
    }

    /* ---- phase 2: unconditional wake, no bit write ---- */
    (VOID)LOS_EventInit(&g_condEvent);
    (VOID)LOS_EventWrite(&g_condEvent, 0x44);   /* satisfies NEITHER 0x11 nor 0x22 */
    g_condState[0] = 0;
    g_condState[1] = 0;
    g_condWoke[0] = 0;
    g_condWoke[1] = 0;
    g_condRet[0] = 0;
    g_condRet[1] = 0;

    ret = SpawnWaiter((TSK_ENTRY_FUNC)TaskF044A, "CondTskA", &waitA);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT2);
    ret = SpawnWaiter((TSK_ENTRY_FUNC)TaskF044B, "CondTskB", &waitB);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT2);

    if (WaitParked(2) != 0) {
        fails++;
        goto EXIT2;
    }

    ret = LOS_EventCondWrite(&g_condEvent);
    if (ret != LOS_OK) {
        fails++;
        goto EXIT2;
    }

    /* Both reads must RETURN (not re-park to their timeouts): woke==1,
     * ret==0 (no bits matched), and the event bits are untouched. */
    (VOID)LOS_TaskJoin(waitA, NULL);
    (VOID)LOS_TaskJoin(waitB, NULL);
    if ((g_condWoke[0] != 1) || (g_condWoke[1] != 1) ||
        (g_condRet[0] != 0) || (g_condRet[1] != 0) ||
        (g_condEvent.uwEventID != 0x44)) {
        fails++;
    }

    /* ---- phase 3a: plain write of NON-matching bits parks on ---- */
    g_condState[0] = 0;
    g_condWoke[0] = 0;
    g_condRet[0] = 0;
    waitA = OS_INVALID;
    ret = SpawnWaiter((TSK_ENTRY_FUNC)TaskF044C, "CondTskC", &waitA);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT3);
    if (WaitParked(1) != 0) {
        fails++;
        goto EXIT3;
    }
    (VOID)LOS_EventWrite(&g_condEvent, 0x22);   /* matches neither 0x11 waiter */
    (VOID)LOS_TaskJoin(waitA, NULL);            /* runs to its 8-tick timeout */
    if ((g_condWoke[0] != 0) || (g_condState[0] != 2)) {
        fails++;   /* a plain write must not release a non-matching waiter */
    }

    /* ---- phase 3b: plain write of matching bits completes the read ---- */
    g_condState[0] = 0;
    g_condWoke[0] = 0;
    g_condRet[0] = 0;
    waitA = OS_INVALID;
    ret = SpawnWaiter((TSK_ENTRY_FUNC)TaskF044D, "CondTskD", &waitA);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT3);
    if (WaitParked(1) != 0) {
        fails++;
        goto EXIT3;
    }
    (VOID)LOS_EventWrite(&g_condEvent, 0x11);
    (VOID)LOS_TaskJoin(waitA, NULL);
    if ((g_condWoke[0] != 1) || (g_condRet[0] != 0x11) || (g_condState[0] != 2)) {
        fails++;
    }
    (VOID)LOS_TaskDelay(2);

EXIT3:
    if (waitA != OS_INVALID) {
        (VOID)LOS_TaskDelete(waitA);
    }
    goto DONE;

EXIT2:
    if (waitA != OS_INVALID) {
        (VOID)LOS_TaskDelete(waitA);
    }
    if (waitB != OS_INVALID) {
        (VOID)LOS_TaskDelete(waitB);
    }

DONE:
    (VOID)LOS_EventDestroy(&g_condEvent);
    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosEvent044(VOID)
{
    TEST_ADD_CASE("ItLosEvent044", Testcase, TEST_LOS, TEST_EVENT, TEST_LEVEL1, TEST_FUNCTION);
}
