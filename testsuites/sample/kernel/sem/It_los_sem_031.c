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
#include "It_los_sem.h"



#define SEM_READ_UINT32(_register_, _value_) ((_value_) = *((volatile UINT32 *)(_register_)))

static UINT32 g_swTmrID;

static VOID SwtmrF01(void)
{
    UINT32 ret;
    ret = LOS_SemPost(g_usSemID);
    ICUNIT_ASSERT_EQUAL_VOID(ret, LOS_OK, ret);

    /* 终判用严格相等: 周期回调在主任务停表前可能重入 */
    if (g_testCount == 0xf) {
        LOS_TaskDelete(g_testTaskID01);
        g_testCount++;
    }
}

static VOID TaskF01(void)
{
    UINT32 ret;

    /* 同步 tick 起点后启动定时器, 保证本任务先就绪再产生首个 post */
    LOS_TaskDelay(1);
    ret = LOS_SwtmrStart(g_swTmrID);
    ICUNIT_ASSERT_EQUAL_VOID(ret, LOS_OK, ret);

    while (1) {
        g_testCount++;
        ret = LOS_SemPend(g_usSemID, LOS_WAIT_FOREVER);
        ICUNIT_ASSERT_EQUAL_VOID(ret, LOS_OK, ret);
    }
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    int value;

    TSK_INIT_PARAM_S task1 = { 0 };

    task1.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF01;
    task1.pcName = "SemTsk31";

    task1.usTaskPrio = 20; // 20, Set the priority of test task to 20.
    task1.uwStackSize = TASK_STACK_SIZE_TEST;

    g_testCount = 0;

    ret = LOS_SemCreate(1, &g_usSemID);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SwtmrCreate(1, LOS_SWTMR_MODE_PERIOD | LOS_SWTMR_ALIGN_FLAG, (SWTMR_PROC_FUNC)SwtmrF01, &g_swTmrID, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    LOS_SemPend(g_usSemID, LOS_NO_WAIT);

    ret = LOS_TaskCreate(&g_testTaskID01, &task1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    while (SEM_READ_UINT32(&g_testCount, value) < 0x10) {
        (void)value;
    }

    LOS_SwtmrStop(g_swTmrID);
    ICUNIT_GOTO_EQUAL(g_testCount, 0x10, g_testCount, EXIT);

    (VOID)LOS_TaskDelete(g_testTaskID01);

    ret = LOS_SwtmrDelete(g_swTmrID);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_SemDelete(g_usSemID);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    return LOS_OK;

EXIT:
    LOS_SwtmrStop(g_swTmrID);
    LOS_SwtmrDelete(g_swTmrID);
    LOS_TaskDelete(g_testTaskID01);
    LOS_SemDelete(g_usSemID);
    return LOS_OK;
}

VOID ItLosSem031(void)
{
    TEST_ADD_CASE("ItLosSem031", Testcase, TEST_LOS, TEST_SEM, TEST_LEVEL1, TEST_FUNCTION);
}

