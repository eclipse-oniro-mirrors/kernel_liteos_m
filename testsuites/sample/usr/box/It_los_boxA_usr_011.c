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

/* ItLosBoxUsr011 - usr-world LOS_TaskSuspend / LOS_TaskResume /
 * LOS_TaskStatusGet through the syscall path. */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root"
#endif

/* los_task_base.h values, mirrored here */
#define USR_TASK_STATUS_SUSPEND 0x0002

static volatile UINT32 g_usr011Ran = 0;

static VOID Usr011Child(VOID)
{
    g_usr011Ran = 1;
    for (;;) {
        LOS_TaskDelay(100);
    }
}

static UINT32 TestCase(VOID)
{
    UINT32 taskId = 0;
    UINT32 status = 0;
    UINT32 ret;
    UINT32 spin;
    TSK_INIT_PARAM_S param = {0};

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)Usr011Child;
    param.uwStackSize = 0x400;
    param.pcName = "Usr011Child";
    param.usTaskPrio = 5;

    g_usr011Ran = 0;
    ret = LOS_TaskCreate(&taskId, &param);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    for (spin = 0; (g_usr011Ran == 0) && (spin < 20); spin++) {
        LOS_TaskDelay(2);
    }
    ICUNIT_ASSERT_EQUAL(g_usr011Ran, 1, g_usr011Ran);

    ret = LOS_TaskSuspend(taskId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_TaskStatusGet(taskId, &status);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(status & USR_TASK_STATUS_SUSPEND, USR_TASK_STATUS_SUSPEND, status);

    ret = LOS_TaskResume(taskId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_TaskDelete(taskId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    LOS_TaskStackRecycle();
    return LOS_OK;
}

VOID ItLosBoxUsr011(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsr011", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
