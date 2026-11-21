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

/* ItLosBoxUsrSwtmr001 - usr-world swtmr query/bring-up surface: the two
 * refs-parity additions to the syscall face.
 *
 * Runs in boxA USER mode.
 *
 *  phase 1 - LOS_SwtmrTimeGet lifecycle on a box timer (encoded id): a
 *     created-but-not-started timer reports NOT_STARTED, a ticking
 *     periodic timer returns LOS_OK with the per-cycle tick in range,
 *     stop flips it back to NOT_STARTED, delete to NOT_CREATED
 *  phase 2 - LOS_SwtmrUsrInit refused with LOS_ERRNO_SWTMR_INIT: the
 *     box swtmr task is a singleton and this box's entry already brought
 *     it up before the usr phase started
 *  phase 3 - the raw-id guard: an id at/above OS_SWTMR_MAX_TIMERID is
 *     rejected as ID_INVALID before any pool walk */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

#define USR_SWT_INTERVAL 2U

STATIC volatile UINT32 g_usrSwtGetCnt = 0;

STATIC VOID UsrSwtGetHandler(UINTPTR arg)
{
    (VOID)arg;
    g_usrSwtGetCnt++;
}

static UINT32 TestCase(VOID)
{
    UINT32 swtmrId = 0;
    UINT32 tick = 0xFFFFFFFFU;
    UINT32 fails = 0;
    UINT32 ret;

    /* ---- phase 1: TimeGet over a box timer's lifecycle ---- */
    ret = LOS_SwtmrCreate(USR_SWT_INTERVAL, LOS_SWTMR_MODE_PERIOD,
                         UsrSwtGetHandler, &swtmrId, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SwtmrTimeGet(swtmrId, &tick);
    if (ret != LOS_ERRNO_SWTMR_NOT_STARTED) {
        fails++;
        LOS_PrintStr("usr swtmr timeget created FAIL\n");
    }

    ret = LOS_SwtmrStart(swtmrId);
    if (ret != LOS_OK) {
        fails++;
        LOS_PrintStr("usr swtmr start FAIL\n");
    } else {
        LOS_TaskDelay(5);    /* more than two periods of interval 2 */
        tick = 0xFFFFFFFFU;
        ret = LOS_SwtmrTimeGet(swtmrId, &tick);
        if ((ret != LOS_OK) || (tick > USR_SWT_INTERVAL)) {
            fails++;
            LOS_PrintStr("usr swtmr timeget ticking FAIL\n");
        }
    }

    if (LOS_SwtmrStop(swtmrId) == LOS_OK) {
        ret = LOS_SwtmrTimeGet(swtmrId, &tick);
        if (ret != LOS_ERRNO_SWTMR_NOT_STARTED) {
            fails++;
            LOS_PrintStr("usr swtmr timeget stopped FAIL\n");
        }
    } else {
        fails++;
        LOS_PrintStr("usr swtmr stop FAIL\n");
    }

    if (LOS_SwtmrDelete(swtmrId) == LOS_OK) {
        ret = LOS_SwtmrTimeGet(swtmrId, &tick);
        if (ret != LOS_ERRNO_SWTMR_NOT_CREATED) {
            fails++;
            LOS_PrintStr("usr swtmr timeget deleted FAIL\n");
        }
    } else {
        fails++;
        LOS_PrintStr("usr swtmr delete FAIL\n");
    }

    /* ---- phase 2: the singleton bring-up is refused over the syscall ---- */
    ret = LOS_SwtmrUsrInit(NULL, 1024);
    if (ret != LOS_ERRNO_SWTMR_INIT) {
        fails++;
        LOS_PrintStr("usr swtmr usr-init singleton FAIL\n");
    }

    /* ---- phase 3: raw-id guards ---- */
    /* Below the raw ceiling the id walks the box decode (high half is not
     * a live box) and fails as NOT_CREATED. */
    ret = LOS_SwtmrTimeGet(0xFFFFFF00U, &tick);
    if (ret != LOS_ERRNO_SWTMR_NOT_CREATED) {
        fails++;
        LOS_PrintStr("usr swtmr timeget decode-id FAIL\n");
    }
    /* At/above OS_SWTMR_MAX_TIMERID the raw guard rejects before any
     * pool walk. */
    ret = LOS_SwtmrTimeGet(0xFFFFFFF0U, &tick);
    if (ret != LOS_ERRNO_SWTMR_ID_INVALID) {
        fails++;
        LOS_PrintStr("usr swtmr timeget raw-id FAIL\n");
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrSwtmr001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrSwtmr001", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
