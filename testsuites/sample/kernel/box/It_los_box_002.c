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
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ItLosBox002 - kernel-mode bookkeeping is stable across a task switch.
 *
 * A child task created by the test task also runs in kernel mode: after the
 * scheduler has switched into it and back (PendSV updates g_curBoxId on
 * EVERY switch - kernel tasks carry OS_KERNEL_BOX), both the child and the
 * parent must still be classified as kernel context. */

#include "osTest.h"
#include "It_los_box.h"
#include "los_box_pri.h"
#include "los_task.h"

#define BOX_TEST_CHILD_STACK_SIZE 0x400

static volatile UINT32 g_childBoxChecked;

static VOID ChildTaskEntry(UINT32 arg)
{
    (void)arg;
    /* 1 = kernel context as expected, 2 = a box was wrongly attached */
    g_childBoxChecked = (OsGetCurBoxCB() == NULL) ? 1 : 2;

    /* Park the child so the parent deletes it deterministically. */
    LOS_TaskDelay(LOSCFG_BASE_CORE_TICK_PER_SECOND * 10);
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 taskID;
    TSK_INIT_PARAM_S taskParam = {0};

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)ChildTaskEntry;
    taskParam.usTaskPrio = TASK_PRIO_TEST_NORMAL;
    taskParam.pcName = "BoxChildTask";
    taskParam.uwStackSize = BOX_TEST_CHILD_STACK_SIZE;

    g_childBoxChecked = 0;
    ret = LOS_TaskCreate(&taskID, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* Child (prio 20) preempts the test task (prio 25): by the time we
     * resume, the scheduler has switched into the child and back. */
    LOS_TaskDelay(2);
    ICUNIT_ASSERT_EQUAL(g_childBoxChecked, 1, g_childBoxChecked);

    /* The parent is still kernel context after the round trip. */
    ICUNIT_ASSERT_EQUAL(OsGetCurBoxCB(), NULL, (UINTPTR)OsGetCurBoxCB());

    (VOID)LOS_TaskDelete(taskID);
    return LOS_OK;
}

VOID ItLosBox002(VOID)
{
    TEST_ADD_CASE("ItLosBox002", TestCase, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
}
