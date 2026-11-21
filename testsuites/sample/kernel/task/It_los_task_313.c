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

#include "osTest.h"
#include "It_los_task.h"

#ifdef LOSCFG_TASK_STACK_STATIC_ALLOCATION

/* 用例简要描述: 静态栈创建形态下非法栈顶（未对齐）被拒绝，合法栈顶不受影响 */
STATIC CHAR g_stack313[OS_TSK_TEST_STACK_SIZE] __attribute__((aligned(16)));

static VOID TaskF01(VOID)
{
    g_testCount++;

    (VOID)LOS_TaskSuspend(g_testTaskID01);
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    TSK_INIT_PARAM_S task1 = { 0 };

    task1.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF01;
    task1.uwStackSize = sizeof(g_stack313);
    task1.pcName = "Tsk313A";
    task1.usTaskPrio = TASK_PRIO_TEST - 1;
    task1.uwResved = LOS_TASK_STATUS_DETACHED;

    g_testCount = 0;

    /* stack top not aligned: rejected, no TCB is taken */
    ret = LOS_TaskCreateOnlyStatic(&g_testTaskID01, &task1, (VOID *)((UINTPTR)g_stack313 + 4));
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_TSK_STKSZ_NOT_ALIGN, ret, EXIT);
    ICUNIT_GOTO_EQUAL(g_testCount, 0, g_testCount, EXIT);

    /* stack size not aligned: rejected */
    task1.uwStackSize = sizeof(g_stack313) - 4;
    ret = LOS_TaskCreateOnlyStatic(&g_testTaskID01, &task1, g_stack313);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_TSK_STKSZ_NOT_ALIGN, ret, EXIT);

    /* legal stack still works: the rejected creates left no residue */
    task1.uwStackSize = sizeof(g_stack313);
    ret = LOS_TaskCreateOnlyStatic(&g_testTaskID01, &task1, g_stack313);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_TaskResume(g_testTaskID01);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT);

EXIT:
    (VOID)LOS_TaskDelete(g_testTaskID01);

    return LOS_OK;
}

VOID ItLosTask313(VOID)
{
    TEST_ADD_CASE("ItLosTask313", TestCase, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
}

#endif
