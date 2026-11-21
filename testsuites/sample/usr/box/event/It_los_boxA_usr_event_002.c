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
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
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

/* ItLosBoxUsrEvent002 - deep blocking half of the SID event family.
 *
 * Runs in boxA USER mode through the shared iCunit. Same verdict shape as
 * ItLosBoxUsrCondvar001: a "fails counter, phases keep running" body (every phase
 * recycles before the next one creates), one ICunit assertion collapses
 * the verdict at the end.
 *
 *  phase 1 - cross-task pend + wake: waiter parks in
 *     LOS_EventReadWithSId(FOREVER); write releases it with the exact mask
 *  phase 2 - bounded pend, no writer -> LOS_ERRNO_EVENT_READ_TIMEOUT (and
 *     the timeout bit must not leak into the next reuse of the slot pair)
 *  phase 3 - WriteOnce wakes the HEAD waiter ONLY: poll the wakeup count
 *     until at least one returned, settle, then judge == 3 (2 both returned,
 *     4 = once behaved like a plain write); a second plain write releases the
 *     survivor */

#include "osTest.h"
#include "syscalls.h"

/* User-stack recycler (kernel/box/los_usr_api.c). No header carries a prototype:
 * the function is a usr-world WRAPPER (it loops OsTaskGetRecycleStack - svc 39 -
 * and frees each stack through the COMMON allocator), not a syscall entry, so
 * the usr include root only #undefs the rename for the name. boxA_main.c and
 * mem/It_los_boxA_usr_mem_util.h declare it the same way. */
extern VOID LOS_TaskStackRecycle(VOID);

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

#define USR_EVT_MASK 0x2U

STATIC volatile UINT32 g_usrEvtSid = 0;
/* 0 = not started, 1 = parked (about to pend / pending), 2 = read returned */
STATIC volatile UINT32 g_usrEvtState[2] = {0, 0};
STATIC volatile UINT32 g_usrEvtResult[2] = {0, 0};

STATIC VOID UsrEvtWaiterRun(UINT32 idx, UINT32 timeout)
{
    g_usrEvtState[idx] = 1;
    g_usrEvtResult[idx] = LOS_EventReadWithSId(g_usrEvtSid, USR_EVT_MASK,
                                               LOS_WAITMODE_OR | LOS_WAITMODE_CLR, timeout);
    g_usrEvtState[idx] = 2;
    /* return -> OsUsrTaskExit trampoline -> detached delete */
}

STATIC VOID UsrEvtWaiterA(VOID) { UsrEvtWaiterRun(0, LOS_WAIT_FOREVER); }
STATIC VOID UsrEvtWaiterB(VOID) { UsrEvtWaiterRun(1, LOS_WAIT_FOREVER); }
STATIC VOID UsrEvtWaiterT(VOID) { UsrEvtWaiterRun(0, 20); }

STATIC UINT32 UsrEvtSpawn(VOID (*entry)(VOID), CHAR *name, UINT32 *taskId)
{
    TSK_INIT_PARAM_S initParam = {0};

    initParam.pfnTaskEntry = (TSK_ENTRY_FUNC)entry;
    initParam.uwStackSize = 1024;
    initParam.pcName = name;
    initParam.usTaskPrio = 5;
    return LOS_TaskCreate(taskId, &initParam);
}

/* Wait until waiters [0, n) have all PARKED. Polls rather than sleeping
 * once: a fixed delay races against equal-priority contention. */
STATIC UINT32 UsrEvtWaitParked(UINT32 n)
{
    UINT32 i;
    UINT32 spin;
    UINT32 bad = 0;

    for (spin = 0; spin < 20; spin++) {
        for (i = 0; (i < n) && (g_usrEvtState[i] == 1); i++) {
        }
        if (i == n) {
            break;
        }
        LOS_TaskDelay(2);
    }
    for (i = 0; i < n; i++) {
        if (g_usrEvtState[i] != 1) {
            bad++;
        }
    }
    return bad;
}

static UINT32 TestCase(VOID)
{
    UINT32 sId = 0;
    UINT32 fails = 0;
    UINT32 waitA = 0;
    UINT32 waitB = 0;
    UINT32 spin;
    UINT32 ret;

    ret = LOS_EventCreate(&sId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    g_usrEvtSid = sId;

    /* ---- phase 1: blocking pend, released by a cross-task write ---- */
    g_usrEvtState[0] = 0;
    g_usrEvtResult[0] = 0;
    if (UsrEvtSpawn(UsrEvtWaiterA, "uEvtWakeA", &waitA) != LOS_OK) {
        fails++;
        LOS_PrintStr("usr evt deep spawn A FAIL\n");
    } else {
        if (UsrEvtWaitParked(1) != 0) {
            fails++;
            LOS_PrintStr("usr evt deep park FAIL\n");
        } else if (LOS_EventWriteWithSId(sId, USR_EVT_MASK) != LOS_OK) {
            fails++;
            LOS_PrintStr("usr evt deep write FAIL\n");
        } else {
            for (spin = 0; (g_usrEvtState[0] != 2) && (spin < 20); spin++) {
                LOS_TaskDelay(2);
            }
            if ((g_usrEvtState[0] != 2) || (g_usrEvtResult[0] != USR_EVT_MASK)) {
                fails++;
                LOS_PrintStr("usr evt deep wake FAIL\n");
            }
        }
        LOS_TaskDelay(5);
        LOS_TaskStackRecycle();
    }

    /* ---- phase 2: bounded pend, no writer -> READ_TIMEOUT ---- */
    g_usrEvtState[0] = 0;
    g_usrEvtResult[0] = 0;
    if (UsrEvtSpawn(UsrEvtWaiterT, "uEvtTmoA", &waitA) != LOS_OK) {
        fails++;
        LOS_PrintStr("usr evt deep spawn T FAIL\n");
    } else {
        for (spin = 0; (g_usrEvtState[0] != 2) && (spin < 40); spin++) {
            LOS_TaskDelay(2);
        }
        if ((g_usrEvtState[0] != 2) || (g_usrEvtResult[0] != LOS_ERRNO_EVENT_READ_TIMEOUT)) {
            fails++;
            LOS_PrintStr("usr evt deep timeout FAIL\n");
        }
        LOS_TaskDelay(5);
        LOS_TaskStackRecycle();
    }

    /* ---- phase 3: WriteOnce wakes the head waiter only ---- */
    g_usrEvtState[0] = 0;
    g_usrEvtState[1] = 0;
    g_usrEvtResult[0] = 0;
    g_usrEvtResult[1] = 0;
    if ((UsrEvtSpawn(UsrEvtWaiterA, "uEvtOnceA", &waitA) != LOS_OK) ||
        (UsrEvtSpawn(UsrEvtWaiterB, "uEvtOnceB", &waitB) != LOS_OK)) {
        fails++;
        LOS_PrintStr("usr evt deep spawn pair FAIL\n");
    } else {
        if (UsrEvtWaitParked(2) != 0) {
            fails++;
            LOS_PrintStr("usr evt deep pair park FAIL\n");
        } else if (LOS_EventWriteOnceWithSId(sId, USR_EVT_MASK) != LOS_OK) {
            fails++;
            LOS_PrintStr("usr evt deep write-once FAIL\n");
        } else {
            /* poll until >=1 returned, settle, THEN judge == 3: polling on
             * "== 3" alone could catch the transient on the way to 4 and
             * mask a woke-both bug. */
            for (spin = 0; ((g_usrEvtState[0] + g_usrEvtState[1]) < 3) && (spin < 20); spin++) {
                LOS_TaskDelay(2);
            }
            LOS_TaskDelay(5);
            if ((g_usrEvtState[0] + g_usrEvtState[1]) != 3) {
                fails++;
                LOS_PrintStr("usr evt deep once-count FAIL\n");
            }
            (VOID)LOS_EventWriteWithSId(sId, USR_EVT_MASK);   /* release survivor */
            for (spin = 0; ((g_usrEvtState[0] != 2) || (g_usrEvtState[1] != 2)) &&
                           (spin < 20); spin++) {
                LOS_TaskDelay(2);
            }
            if ((g_usrEvtState[0] != 2) || (g_usrEvtState[1] != 2)) {
                fails++;
                LOS_PrintStr("usr evt deep second-wake FAIL\n");
            }
        }
        LOS_TaskDelay(5);
        LOS_TaskStackRecycle();
    }

    (VOID)LOS_EventDelete(sId);
    g_usrEvtSid = 0;

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrEvent002(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrEvent002", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
