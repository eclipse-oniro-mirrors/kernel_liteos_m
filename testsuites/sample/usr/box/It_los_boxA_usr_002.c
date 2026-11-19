/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the following conditions are met:
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
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ItLosBoxUsr002 - usr-world semaphore lifecycle through the SID syscalls.
 *
 * create (boxA pool SID) -> pend-timeout on count=0 (blocking schedule
 * out/in through the SVC graft) -> post -> immediate pend -> delete; the
 * second round proves the pool slot is recycled. */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs (testsuites:include / box_syscalls must not reach here)"
#endif

static UINT32 TestCase(VOID)
{
    UINT32 sId = 0;
    UINT32 ret;
    UINT32 fails = 0;
    UINT32 round;

    for (round = 0; round < 2; round++) {
        ret = LOS_SemCreate(0, &sId);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

        /* pend-timeout window: 10 ticks (~10s on this board's ~1s tick). */
        if (LOS_SemPend(sId, 10) != LOS_ERRNO_SEM_TIMEOUT) {
            fails++;   /* count == 0: full timeout through the SVC path */
        }
        if (LOS_SemPost(sId) != LOS_OK) {
            fails++;
        }
        if (LOS_SemPend(sId, 0) != LOS_OK) {
            fails++;   /* count == 1: immediate take */
        }
        if (LOS_SemDelete(sId) != LOS_OK) {
            fails++;
        }
    }
    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsr002(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsr002", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
