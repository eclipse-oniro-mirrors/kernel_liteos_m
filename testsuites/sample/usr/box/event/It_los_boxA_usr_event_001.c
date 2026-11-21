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

/* ItLosBoxUsrEvent001 - usr-world SID event Clear: LOS_EventClearWithSId.
 *
 * Runs in boxA USER mode. Clear had a syscall slot since the event-pool
 * block was wired but no case ever exercised it - this is its first cover.
 *
 * CONTRACT (kernel/box/los_event_sid.c, matching the vanilla
 * LOS_EventClear at kernel/base/los_event.c): the events argument is a
 * KEEP mask - the event retains ONLY those bits, everything else is
 * cleared (uwEventID &= events). NOT "clear the given bits".
 *
 *  phase 1 - partial keep: write 0x11, keep 0x01 -> the surviving event
 *     is exactly 0x01 (poll OR|CLR reads it back and empties it)
 *  phase 2 - keep-mask of UNSET bits empties the event: write 0x11,
 *     keep 0x04 (not present) -> poll OR returns 0
 *  phase 3 - the negative path: a stale sId is refused */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

static UINT32 TestCase(VOID)
{
    UINT32 sId = 0;
    UINT32 val;
    UINT32 fails = 0;
    UINT32 ret;

    ret = LOS_EventCreate(&sId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* ---- phase 1: partial keep, the surviving bit stays readable ---- */
    if (LOS_EventWriteWithSId(sId, 0x11U) != LOS_OK) {
        fails++;
        LOS_PrintStr("usr evt clear write FAIL\n");
    } else if (LOS_EventClearWithSId(sId, 0x01U) != LOS_OK) {
        fails++;
        LOS_PrintStr("usr evt clear FAIL\n");
    } else {
        val = LOS_EventPollWithSId(sId, 0x01U, LOS_WAITMODE_OR | LOS_WAITMODE_CLR);
        if (val != 0x01U) {
            fails++;
            LOS_PrintStr("usr evt clear poll FAIL\n");
        }
    }

    /* ---- phase 2: keeping only UNSET bits empties the event ---- */
    if (LOS_EventWriteWithSId(sId, 0x11U) != LOS_OK) {
        fails++;
        LOS_PrintStr("usr evt clear2 write FAIL\n");
    } else if (LOS_EventClearWithSId(sId, 0x04U) != LOS_OK) {
        fails++;
        LOS_PrintStr("usr evt clear2 FAIL\n");
    } else {
        val = LOS_EventPollWithSId(sId, 0x11U, LOS_WAITMODE_OR);
        if (val != 0) {
            fails++;
            LOS_PrintStr("usr evt clear2 poll FAIL\n");
        }
    }

    /* ---- phase 3: stale sId refused ---- */
    if (LOS_EventDelete(sId) != LOS_OK) {
        fails++;
        LOS_PrintStr("usr evt clear delete FAIL\n");
    } else {
        ret = LOS_EventClearWithSId(sId, 0x01U);
        if (ret == LOS_OK) {
            fails++;
            LOS_PrintStr("usr evt clear stale FAIL\n");
        }
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrEvent001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrEvent001", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
