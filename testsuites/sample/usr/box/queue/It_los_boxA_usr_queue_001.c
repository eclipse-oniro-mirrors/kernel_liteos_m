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
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
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

/* ItLosBoxUsrQueue001 - shallow SID queue lifecycle.
 *
 * Runs in boxA USER mode through the shared iCunit. Same verdict shape as
 * ItLosBoxUsrCondvar001/Event002/Swtmr002: a "fails counter, phases keep running" body (a
 * failed phase still deletes what it created), one ICunit assertion
 * collapses the verdict at the end.
 *
 *  phase 1 - create routes into THIS box's pool (LOSCFG_BOXA_QUEUE_LIMIT
 *     == 1, the handle comes back SID-encoded box-head), write a payload
 *     by value, read it back and check value + size
 *  phase 2 - pool quota: with the single CB held, a second create must
 *     fail; delete must give the slot back (a fresh create succeeds) */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

static UINT32 TestCase(VOID)
{
    UINT32 queueID = 0;
    UINT32 msgOut = 0xA5A51234U;
    UINT32 msgIn = 0;
    UINT32 readSize = sizeof(UINT32);
    UINT32 fails = 0;
    UINT32 ret;

    /* Under BOX the create routes into this box's pool and returns a
     * box-head sId; the kernel baseline suite proves the kernel pool still
     * serves kernel contexts in parallel. */
    ret = LOS_QueueCreate("usrQ06", 4, &queueID, 0, sizeof(UINT32));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* ---- phase 1: write by value, read back, verify payload + size ---- */
    ret = LOS_QueueWriteCopy(queueID, &msgOut, sizeof(UINT32), LOS_NO_WAIT);
    if (ret != LOS_OK) {
        fails++;
        LOS_PrintStr("usr queue write FAIL\n");
    } else {
        ret = LOS_QueueReadCopy(queueID, &msgIn, &readSize, LOS_NO_WAIT);
        if ((ret != LOS_OK) || (msgIn != msgOut) || (readSize != sizeof(UINT32))) {
            fails++;
            LOS_PrintStr("usr queue read FAIL\n");
        }
    }

    /* ---- phase 2: box-pool quota - the pool holds exactly ONE queue CB ---- */
    {
        UINT32 q2 = 0;
        UINT32 q3 = 0;

        if (LOS_QueueCreate("usrQ06b", 4, &q2, 0, sizeof(UINT32)) == LOS_OK) {
            fails++;
            LOS_PrintStr("usr queue pool exhaustion FAIL\n");
            (VOID)LOS_QueueDelete(q2);
        }
        if (LOS_QueueDelete(queueID) != LOS_OK) {
            fails++;
            LOS_PrintStr("usr queue delete FAIL\n");
        }
        if (LOS_QueueCreate("usrQ06c", 4, &q3, 0, sizeof(UINT32)) != LOS_OK) {
            fails++;
            LOS_PrintStr("usr queue slot recycle FAIL\n");
        } else {
            (VOID)LOS_QueueDelete(q3);
        }
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrQueue001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrQueue001", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
