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

/* ItLosBoxUsrEvent003 - shallow SID event pool lifecycle.
 *
 * Runs in boxA USER mode through the shared iCunit. Straight
 * create -> write -> poll(read-and-clear) -> delete chain on this box's
 * event pool, including the delete-return check; "fails counter" verdict
 * shape as in 003-006. This is the ONLY existing usr-side cover of
 * LOS_EventPollWithSId (the deep case 004 exercises Read/Write/WriteOnce
 * instead). */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

#define USR_EVT_SHALLOW_MASK 0x11U

static UINT32 TestCase(VOID)
{
    UINT32 sId = 0;
    UINT32 fails = 0;
    UINT32 val;
    UINT32 ret;

    ret = LOS_EventCreate(&sId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    if (LOS_EventWriteWithSId(sId, USR_EVT_SHALLOW_MASK) != LOS_OK) {
        fails++;
        LOS_PrintStr("usr evt shallow write FAIL\n");
    } else {
        /* poll = read-and-clear: the full mask must come back exactly once */
        val = LOS_EventPollWithSId(sId, USR_EVT_SHALLOW_MASK,
                                   LOS_WAITMODE_OR | LOS_WAITMODE_CLR);
        if (val != USR_EVT_SHALLOW_MASK) {
            fails++;
            LOS_PrintStr("usr evt shallow poll FAIL\n");
        }
    }

    if (LOS_EventDelete(sId) != LOS_OK) {
        fails++;
        LOS_PrintStr("usr evt shallow delete FAIL\n");
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrEvent003(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrEvent003", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
