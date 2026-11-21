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
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
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

/* ItLosBoxUsr013 - usr-world LOS_TaskLock / LOS_TaskUnlock through the
 * syscall path: while the scheduler is locked a newly created same-priority
 * task must not run; unlock + yield lets it run. */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root"
#endif

static volatile UINT32 g_usr013Ran = 0;

static VOID Usr013Child(VOID)
{
    g_usr013Ran = 1;
}

static UINT32 TestCase(VOID)
{
    UINT32 taskId = 0;
    UINT32 ret;
    UINT32 spin;
    TSK_INIT_PARAM_S param = {0};

    /* same priority as the runner: only a yield/time slice could schedule it */
    param.pfnTaskEntry = (TSK_ENTRY_FUNC)Usr013Child;
    param.uwStackSize = 0x400;
    param.pcName = "Usr013Child";
    param.usTaskPrio = 5;

    g_usr013Ran = 0;
    LOS_TaskLock();
    ret = LOS_TaskCreate(&taskId, &param);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(g_usr013Ran, 0, g_usr013Ran);
    LOS_TaskUnlock();

    for (spin = 0; (g_usr013Ran == 0) && (spin < 20); spin++) {
        LOS_TaskDelay(2);
    }
    ICUNIT_ASSERT_EQUAL(g_usr013Ran, 1, g_usr013Ran);

    LOS_TaskStackRecycle();
    return LOS_OK;
}

VOID ItLosBoxUsr013(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsr013", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
