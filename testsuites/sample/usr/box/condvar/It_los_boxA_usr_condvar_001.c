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

/* ItLosBoxUsrCondvar001 - condvar four-phase.
 *
 * Runs in boxA USER mode through the shared iCunit. The body keeps the
 * "fails counter, no early return" shape on purpose: a waiter left parked
 * holds the condvar count positive, OsCondVarDelete then refuses it and the
 * pool node leaks - with boxA COND_LIMIT=2, two leaked rounds make every
 * later create fail. Phases keep running, cleanup always runs, and ONE
 * ICunit assertion at the end collapses the verdict; per-phase LOS_PrintStr
 * diagnostics stay for localization.
 *
 *  phase 1 - one parked waiter woken by signal (parent holds the mutex while
 *            signalling: closes the state==1/count-not-yet-bumped window)
 *  phase 2 - two parked waiters, broadcast must wake BOTH (only two waiters
 *            separate once from broadcast)
 *  phase 3 - bounded wait with no signaler -> timeout, and the count rollback
 *            is proven indirectly: delete must succeed (it refuses count>0)
 *  phase 4 - both the pool node and the SID slot came back (recreate+delete) */

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

#if defined(LOSCFG_BASE_IPC_CONDVAR) && (LOSCFG_BASE_IPC_CONDVAR == 1)

STATIC volatile UINT32 g_usrCondSid = 0;
STATIC volatile UINT32 g_usrCondMux = 0;
/* 0 = not started, 1 = parked inside OsCondVarWait, 2 = the wait returned */
STATIC volatile UINT32 g_usrCondState[2] = {0, 0};
STATIC volatile UINT32 g_usrCondResult[2] = {0, 0};

STATIC VOID UsrCondWaiterBody(UINT32 idx, UINT32 ticks)
{
    if (LOS_MuxPend(g_usrCondMux, LOS_WAIT_FOREVER) != LOS_OK) {
        g_usrCondResult[idx] = LOS_NOK;
        g_usrCondState[idx] = 2;
        return;
    }
    g_usrCondState[idx] = 1;
    g_usrCondResult[idx] = OsCondVarWait(g_usrCondSid, g_usrCondMux, ticks);
    g_usrCondState[idx] = 2;
    (VOID)LOS_MuxPost(g_usrCondMux);
}

STATIC VOID UsrCondWaiterA(VOID) { UsrCondWaiterBody(0, LOS_WAIT_FOREVER); }
STATIC VOID UsrCondWaiterB(VOID) { UsrCondWaiterBody(1, LOS_WAIT_FOREVER); }

/* Park waiters [0, n) and count the ones that never really blocked. Polls
 * instead of a fixed sleep: a single fixed delay races against equal-prio
 * load. */
STATIC UINT32 UsrCondParkCheck(UINT32 n)
{
    UINT32 i;
    UINT32 spin;
    UINT32 bad = 0;

    for (spin = 0; spin < 20; spin++) {
        for (i = 0; (i < n) && (g_usrCondState[i] == 1); i++) {
        }
        if (i == n) {
            break;
        }
        LOS_TaskDelay(2);
    }
    for (i = 0; i < n; i++) {
        if (g_usrCondState[i] != 1) {
            bad++;
        }
    }
    return bad;
}

/* Wait for waiters [0, n) to leave OsCondVarWait, all with LOS_OK. */
STATIC UINT32 UsrCondWakeCheck(UINT32 n)
{
    UINT32 i;
    UINT32 spin;
    UINT32 bad = 0;

    for (spin = 0; spin < 20; spin++) {
        for (i = 0; (i < n) && (g_usrCondState[i] == 2); i++) {
        }
        if (i == n) {
            break;
        }
        LOS_TaskDelay(2);
    }
    for (i = 0; i < n; i++) {
        if ((g_usrCondState[i] != 2) || (g_usrCondResult[i] != LOS_OK)) {
            bad++;
        }
    }
    return bad;
}

STATIC UINT32 UsrSpawnTask(VOID (*entry)(VOID), CHAR *name, UINT32 *taskId)
{
    TSK_INIT_PARAM_S initParam = {0};

    initParam.pfnTaskEntry = (TSK_ENTRY_FUNC)entry;
    initParam.uwStackSize = 1024;
    initParam.pcName = name;
    initParam.usTaskPrio = 5;
    return LOS_TaskCreate(taskId, &initParam);
}

static UINT32 TestCase(VOID)
{
    UINT32 condSid = 0;
    UINT32 muxId = 0;
    UINT32 fails = 0;
    UINT32 waitA = 0;
    UINT32 waitB = 0;
    UINT32 sret;
    BOOL condAlive = FALSE;

    UINT32 ret;

    ret = LOS_MuxCreate(&muxId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    /* OsCondVarCreate's argument is IN/OUT: 0 means allocate a fresh sId. */
    condSid = 0;
    if (OsCondVarCreate(&condSid) != LOS_OK) {
        fails++;
        LOS_PrintStr("usr condvar create FAIL\n");
    } else {
        condAlive = TRUE;
    }
    g_usrCondSid = condSid;
    g_usrCondMux = muxId;

    /* ---- phase 1: one parked waiter, woken by signal (mutex held) ---- */
    if (fails == 0) {
        g_usrCondState[0] = 0;
        g_usrCondResult[0] = 0;
        if (UsrSpawnTask(UsrCondWaiterA, "uCondWakeA", &waitA) != LOS_OK) {
            fails++;
            LOS_PrintStr("usr condvar spawn A FAIL\n");
        } else if (UsrCondParkCheck(1) != 0) {
            fails++;
            LOS_PrintStr("usr condvar park FAIL\n");
        } else if (LOS_MuxPend(muxId, LOS_WAIT_FOREVER) != LOS_OK) {
            fails++;
            LOS_PrintStr("usr condvar p1 pend FAIL\n");
        } else {
            sret = OsCondVarSignal(condSid, FALSE);
            (VOID)LOS_MuxPost(muxId);
            if (sret != LOS_OK) {
                fails++;
                LOS_PrintStr("usr condvar signal FAIL\n");
            } else if (UsrCondWakeCheck(1) != 0) {
                fails++;
                LOS_PrintStr("usr condvar wake FAIL\n");
            }
        }
        LOS_TaskDelay(5);              /* let the child exit, idle recycle */
        LOS_TaskStackRecycle();
    }

    /* ---- phase 2: two parked waiters, both woken by broadcast ---- */
    if (fails == 0) {
        g_usrCondState[0] = 0;
        g_usrCondState[1] = 0;
        g_usrCondResult[0] = 0;
        g_usrCondResult[1] = 0;
        if ((UsrSpawnTask(UsrCondWaiterA, "uCondBcA", &waitA) != LOS_OK) ||
            (UsrSpawnTask(UsrCondWaiterB, "uCondBcB", &waitB) != LOS_OK)) {
            fails++;
            LOS_PrintStr("usr condvar spawn BC FAIL\n");
        } else if (UsrCondParkCheck(2) != 0) {
            fails++;
            LOS_PrintStr("usr condvar park2 FAIL\n");
        } else if (LOS_MuxPend(muxId, LOS_WAIT_FOREVER) != LOS_OK) {
            fails++;
            LOS_PrintStr("usr condvar p2 pend FAIL\n");
        } else {
            sret = OsCondVarSignal(condSid, TRUE);
            (VOID)LOS_MuxPost(muxId);
            if (sret != LOS_OK) {
                fails++;
                LOS_PrintStr("usr condvar broadcast FAIL\n");
            } else if (UsrCondWakeCheck(2) != 0) {
                fails++;               /* broadcast woke only the head */
                LOS_PrintStr("usr condvar wake2 FAIL\n");
            }
        }
        LOS_TaskDelay(5);
        LOS_TaskStackRecycle();
    }

    /* ---- phase 3: bounded wait -> timeout + count rollback ---- */
    if (fails == 0) {
        UINT32 wret;

        if (LOS_MuxPend(muxId, LOS_WAIT_FOREVER) != LOS_OK) {
            fails++;
            LOS_PrintStr("usr condvar p3 pend FAIL\n");
        } else {
            wret = OsCondVarWait(condSid, muxId, 20);
            (VOID)LOS_MuxPost(muxId);
            if (wret == LOS_OK) {
                fails++;               /* nobody signalled - must not succeed */
                LOS_PrintStr("usr condvar timeout FAIL\n");
            }
        }
        /* delete succeeding IS the rollback evidence (it refuses count>0) */
        if (OsCondVarDelete(condSid) != LOS_OK) {
            fails++;
            LOS_PrintStr("usr condvar delete FAIL\n");
        } else {
            condAlive = FALSE;
        }
    }

    /* ---- phase 4: pool node + SID slot both recycled ---- */
    if (fails == 0) {
        condSid = 0;
        if (OsCondVarCreate(&condSid) != LOS_OK) {
            fails++;
            LOS_PrintStr("usr condvar recreate FAIL\n");
        } else {
            condAlive = TRUE;
            if (OsCondVarDelete(condSid) != LOS_OK) {
                fails++;
                LOS_PrintStr("usr condvar recreate del FAIL\n");
            } else {
                condAlive = FALSE;
            }
        }
    }

    /* Cleanup always runs: one jittery case must not poison the usr suite. */
    if (condAlive) {
        (VOID)OsCondVarSignal(condSid, TRUE);
        LOS_TaskDelay(10);
        LOS_TaskStackRecycle();
        (VOID)OsCondVarDelete(condSid);
    }
    (VOID)LOS_MuxDelete(muxId);

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrCondvar001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrCondvar001", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
#endif /* LOSCFG_BASE_IPC_CONDVAR */
