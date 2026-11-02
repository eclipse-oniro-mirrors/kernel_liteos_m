/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
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

#include "osTest.h"
#include "It_los_trace.h"

/*
 * @tc.name      ItLosTrace010
 * @tc.desc      LOS_TraceStart branch coverage: repeated start (TRACE_STARTED),
 *               stop -> restart normal path, restart repeatability.
 * @tc.coverage interface: LOS_TraceStart / LOS_TraceStop / OsTraceIsEnable;
 *              branch: TRACE_STARTED -> start no-op (los_trace.c:407),
 *              TRACE_STOPED -> start normal path (los_trace.c:417-424).
 *              The TRACE_UNINIT branch (los_trace.c:411-415) is NOT reachable
 *              here: the boot SYS_INIT has already run LOS_TraceInit, and no
 *              public API can move the state back to TRACE_UNINIT.
 *              Placement note: runs before ItLosTrace006, which intentionally
 *              leaves trace STOPPED at the tail of the suite; this case ends
 *              in the STARTED state, same as the boot-time default (OFFLINE).
 */
static UINT32 Testcase(VOID)
{
    UINT32 ret;

    /* Normalize the precondition: stop first (no-op if already stopped), then
     * start, so the case does not depend on the state left by previous cases. */
    LOS_TraceStop();
    ret = LOS_TraceStart();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(OsTraceIsEnable(), TRUE, OsTraceIsEnable());

    /* F-Pos: repeated start while running (TRACE_STARTED branch) -> LOS_OK,
     * idempotent, still enabled. */
    ret = LOS_TraceStart();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(OsTraceIsEnable(), TRUE, OsTraceIsEnable());

    /* F-Pos: stop -> restart: the normal start path (OsTraceNotifyStart +
     * enable + LOS_TRACE(MEM_INFO_REQ)) is covered. */
    LOS_TraceStop();
    ICUNIT_ASSERT_EQUAL(OsTraceIsEnable(), FALSE, OsTraceIsEnable());
    ret = LOS_TraceStart();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(OsTraceIsEnable(), TRUE, OsTraceIsEnable());

    /* F-Pos: the restart path is repeatable. Ends in STARTED, restoring the
     * boot-time default state (OFFLINE mode boots as STARTED). */
    LOS_TraceStop();
    ICUNIT_ASSERT_EQUAL(OsTraceIsEnable(), FALSE, OsTraceIsEnable());
    ret = LOS_TraceStart();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(OsTraceIsEnable(), TRUE, OsTraceIsEnable());

    return LOS_OK;
}

VOID ItLosTrace010(VOID)
{
    TEST_ADD_CASE("ItLosTrace010", Testcase, TEST_LOS, TEST_TRACE, TEST_LEVEL1, TEST_FUNCTION);
}
