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

/* ItLosBoxUsr005 - deep swtmr coverage.
 *
 * Runs in boxA USER mode through the shared iCunit. Verdict shape as in
 * ItLosBoxUsr003: "bad counter, phases keep running", one ICunit assertion
 * collapses the verdict.
 *
 *  phase 1 - ONCE fires exactly once, then self-deletes: the stale handle
 *     must be refused (checked BEFORE any slot reuse - a box swtmr id is
 *     OS_BOX_SET_ID(owner, in-pool index) and is not rotated on delete)
 *  phase 2 - the box pool is finite: fill it, create must be refused with
 *     LOS_ERRNO_SWTMR_MAXSIZE, free one slot, the refused create succeeds
 *  phase 3 - delete from INSIDE the handler: the kernel hands the box ring a
 *     VALUE COPY (handler+arg), so the delete cannot pull the CB out from
 *     under the running callback; the recycle proof is that both pool slots
 *     become allocatable again with no explicit delete from this task
 *  phase 4 - NO_SELFDELETE fires once and waits for a manual delete: Stop
 *     then reports NOT_STARTED (alive, not ticking), NOT a recycled
 *     NOT_CREATED - that difference is the whole point of the mode */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

#if defined(LOSCFG_BOX) && (LOSCFG_BASE_CORE_SWTMR == 1)

STATIC volatile UINT32 g_usrSwtOnce = 0;
STATIC volatile UINT32 g_usrSwtNoSelf = 0;
STATIC volatile UINT32 g_usrSwtFiller = 0;
STATIC volatile UINT32 g_usrSwtSelfDel = 0;
STATIC volatile UINT32 g_usrSwtSelfId = 0;
STATIC volatile UINT32 g_usrSwtSelfRet = 0;

STATIC VOID UsrSwtOnceHandler(UINTPTR arg)
{
    (VOID)arg;
    g_usrSwtOnce++;
}

STATIC VOID UsrSwtNoSelfHandler(UINTPTR arg)
{
    (VOID)arg;
    g_usrSwtNoSelf++;
}

/* filler for timers that are created but never started: any fire is a bug */
STATIC VOID UsrSwtFillerHandler(UINTPTR arg)
{
    (VOID)arg;
    g_usrSwtFiller++;
}

STATIC VOID UsrSwtSelfDelHandler(UINTPTR arg)
{
    (VOID)arg;
    g_usrSwtSelfDel++;
    if ((g_usrSwtSelfDel == 1U) && (g_usrSwtSelfId != 0U)) {
        g_usrSwtSelfRet = LOS_SwtmrDelete(g_usrSwtSelfId);
    }
}

static UINT32 TestCase(VOID)
{
    UINT32 id1 = 0;
    UINT32 idA = 0;
    UINT32 idB = 0;
    UINT32 idC = 0;
    UINT32 ret;
    UINT32 i;
    UINT32 bad = 0;
    BOOL gotB;
    BOOL gotC;

    g_usrSwtFiller = 0;

    /* ---- phase 1: ONCE fires once, then self-deletes ---- */
    g_usrSwtOnce = 0;
    ret = LOS_SwtmrCreate(5, LOS_SWTMR_MODE_ONCE, UsrSwtOnceHandler, &id1, 0);
    if ((ret == LOS_OK) && (LOS_SwtmrStart(id1) == LOS_OK)) {
        for (i = 0; (i < 40U) && (g_usrSwtOnce == 0U); i++) {
            LOS_TaskDelay(2);
        }
        LOS_TaskDelay(20);              /* ONCE must not re-arm */
        if (g_usrSwtOnce != 1U) {
            LOS_PrintStr("usr swtmr deep p1 count FAIL\n");
            bad++;
        }
        if (LOS_SwtmrStop(id1) != LOS_ERRNO_SWTMR_NOT_CREATED) {
            LOS_PrintStr("usr swtmr deep p1 self-delete FAIL\n");
            bad++;
        }
    } else {
        LOS_PrintStr("usr swtmr deep p1 create/start FAIL\n");
        bad++;
    }

    /* ---- phase 2: box pool exhaustion (LOSCFG_BOXA_SWT_LIMIT) ---- */
    gotB = (LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, UsrSwtFillerHandler, &idA, 0) == LOS_OK);
    gotC = (LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, UsrSwtFillerHandler, &idB, 0) == LOS_OK);
    if (!gotB || !gotC) {
        LOS_PrintStr("usr swtmr deep p2 fill FAIL\n");
        bad++;
    }
    idC = 0;
    ret = LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, UsrSwtFillerHandler, &idC, 0);
    if ((ret != LOS_ERRNO_SWTMR_MAXSIZE) || (idC != 0U)) {
        LOS_PrintStr("usr swtmr deep p2 exhaust FAIL\n");
        bad++;
    }
    if (gotB) {
        (VOID)LOS_SwtmrDelete(idA);    /* also proves phase 1's slot went back
                                        * to the BOX free list, not the kernel's */
    }
    if (LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, UsrSwtFillerHandler, &idC, 0) != LOS_OK) {
        LOS_PrintStr("usr swtmr deep p2 refill FAIL\n");
        bad++;
    } else {
        (VOID)LOS_SwtmrDelete(idC);
    }
    if (gotC) {
        (VOID)LOS_SwtmrDelete(idB);
    }

    /* ---- phase 3: delete from inside the handler ---- */
    g_usrSwtSelfDel = 0;
    g_usrSwtSelfRet = 0xFFFFFFFFU;
    g_usrSwtSelfId = 0;
    ret = LOS_SwtmrCreate(5, LOS_SWTMR_MODE_PERIOD, UsrSwtSelfDelHandler, &idA, 0);
    if (ret != LOS_OK) {
        LOS_PrintStr("usr swtmr deep p3 create FAIL\n");
        bad++;
    } else {
        g_usrSwtSelfId = idA;          /* set BEFORE start: the handler reads it */
        if (LOS_SwtmrStart(idA) != LOS_OK) {
            LOS_PrintStr("usr swtmr deep p3 start FAIL\n");
            bad++;
        }
        for (i = 0; (i < 40U) && (g_usrSwtSelfDel == 0U); i++) {
            LOS_TaskDelay(2);
        }
        LOS_TaskDelay(20);              /* let a deferred delete land */
        if ((g_usrSwtSelfDel < 1U) || (g_usrSwtSelfRet != LOS_OK)) {
            LOS_PrintStr("usr swtmr deep p3 self-delete FAIL\n");
            bad++;
        }
        gotB = (LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, UsrSwtFillerHandler, &idB, 0) == LOS_OK);
        gotC = (LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, UsrSwtFillerHandler, &idC, 0) == LOS_OK);
        if (!gotB || !gotC) {
            LOS_PrintStr("usr swtmr deep p3 recycle FAIL\n");
            bad++;
        }
        if (gotB) {
            (VOID)LOS_SwtmrDelete(idB);
        }
        if (gotC) {
            (VOID)LOS_SwtmrDelete(idC);
        }
        (VOID)LOS_SwtmrStop(idA);      /* safe both ways */
        (VOID)LOS_SwtmrDelete(idA);
    }

    /* ---- phase 4: NO_SELFDELETE fires once, waits for manual delete ---- */
    g_usrSwtNoSelf = 0;
    ret = LOS_SwtmrCreate(5, LOS_SWTMR_MODE_NO_SELFDELETE, UsrSwtNoSelfHandler, &idA, 0);
    if ((ret == LOS_OK) && (LOS_SwtmrStart(idA) == LOS_OK)) {
        for (i = 0; (i < 40U) && (g_usrSwtNoSelf == 0U); i++) {
            LOS_TaskDelay(2);
        }
        LOS_TaskDelay(30);              /* must not re-arm */
        if (g_usrSwtNoSelf != 1U) {
            LOS_PrintStr("usr swtmr deep p4 count FAIL\n");
            bad++;
        }
        if (LOS_SwtmrStop(idA) != LOS_ERRNO_SWTMR_NOT_STARTED) {
            LOS_PrintStr("usr swtmr deep p4 stop FAIL\n");
            bad++;
        }
        if (LOS_SwtmrDelete(idA) != LOS_OK) {
            LOS_PrintStr("usr swtmr deep p4 delete FAIL\n");
            bad++;
        }
    } else {
        LOS_PrintStr("usr swtmr deep p4 create/start FAIL\n");
        bad++;
    }

    /* none of the never-started fillers may ever have fired */
    if (g_usrSwtFiller != 0U) {
        LOS_PrintStr("usr swtmr deep filler fired FAIL\n");
        bad++;
    }

    ICUNIT_ASSERT_EQUAL(bad, 0, bad);
    return LOS_OK;
}

VOID ItLosBoxUsr005(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsr005", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
#endif /* LOSCFG_BOX && LOSCFG_BASE_CORE_SWTMR */
