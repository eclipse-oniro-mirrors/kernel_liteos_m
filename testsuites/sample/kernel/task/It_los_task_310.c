/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the conditions of the LOSCFG-style open source license.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY
 * OR FIT FOR A PARTICULAR PURPOSE.
 */

#include "osTest.h"
#include "It_los_task.h"

/* 用例简要描述: 并发 Join 返 LOS_ERRNO_TSK_ALREADY_JOIN; 回收后 Detach 返 LOS_ERRNO_TSK_NOT_CREATED(Join 竞争 + 已回收任务负向) */
static UINT32 g_tskTid310 = 0xFFFFFFFF;
static UINT32 g_joinRet310 = 0xFFFFFFFF;
static volatile UINT32 g_hDone310 = 0;

static VOID TaskT310(UINT32 arg)
{
    (VOID)arg;
}

static VOID TaskH310(UINT32 arg)
{
    UINTPTR retval = 0;
    (VOID)arg;
    /* 阻塞在 Join(T): T 处于挂起态(非 zombie),helper 在此 pend,joinner=helper */
    g_joinRet310 = LOS_TaskJoin(g_tskTid310, &retval);
    g_hDone310 = 1;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 taskHID = 0xFFFFFFFF;
    UINTPTR retval = 0;
    UINT32 loop;
    TSK_INIT_PARAM_S taskParam = {0};

    /* T: joinable, CreateOnly 创建即挂起(不运行), 优先级低于本任务 */
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskT310;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "Tsk310T";
    taskParam.usTaskPrio = TASK_PRIO_TEST + 1;
    taskParam.uwResved = LOS_TASK_ATTR_JOINABLE;
    g_tskTid310 = 0xFFFFFFFF;
    ret = LOS_TaskCreateOnly(&g_tskTid310, &taskParam);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* H: joinable, 优先级高于本任务 → 创建即抢占, 阻塞在 Join(T) */
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskH310;
    taskParam.pcName = "Tsk310H";
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    g_hDone310 = 0;
    g_joinRet310 = 0xFFFFFFFF;
    ret = LOS_TaskCreate(&taskHID, &taskParam);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Race: H 阻塞在 Join(T) 期间, 本任务并发 Join(T) → LOS_ERRNO_TSK_ALREADY_JOIN (los_task.c:1724) */
    ret = LOS_TaskJoin(g_tskTid310, &retval);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_TSK_ALREADY_JOIN, ret, EXIT);

    /* 唤醒 T: 退出成 zombie → OsTaskJoinPost 唤醒 H 回收 T */
    ret = LOS_TaskResume(g_tskTid310);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 有界等待 H 完成 Join 并回收 T */
    loop = 0;
    while ((g_hDone310 == 0) && (loop < 1000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_hDone310, 1, g_hDone310, EXIT);
    ICUNIT_GOTO_EQUAL(g_joinRet310, LOS_OK, g_joinRet310, EXIT);

    /* F-Inv: T 已被 H 回收(UNUSED) → LOS_TaskDetach(同ID) → LOS_ERRNO_TSK_NOT_CREATED (los_task.c:1756) */
    ret = LOS_TaskDetach(g_tskTid310);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_TSK_NOT_CREATED, ret, EXIT);

    /* 清理: Join 回收 H */
    ret = LOS_TaskJoin(taskHID, &retval);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    return LOS_OK;

EXIT:
    /* 兜底回收: 先尽力唤醒 T 再逐个 Join(T/H); 已回收则返错误码忽略 */
    if (g_tskTid310 != 0xFFFFFFFF) {
        (VOID)LOS_TaskResume(g_tskTid310);
        (VOID)LOS_TaskJoin(g_tskTid310, &retval);
    }
    if (taskHID != 0xFFFFFFFF) {
        (VOID)LOS_TaskJoin(taskHID, &retval);
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_TASK
 * @par TestCase_Number
 * ItLosTask310
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test concurrent Join contention and Detach on a reaped task
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: create suspended joinable task T and helper H (higher prio) which blocks in LOS_TaskJoin(T).
 * step2: main task concurrently joins T, expect LOS_ERRNO_TSK_ALREADY_JOIN.
 * step3: resume T so it exits; H's join reaps it, expect H's join returns LOS_OK.
 * step4: LOS_TaskDetach(T) after reaping, expect LOS_ERRNO_TSK_NOT_CREATED; join H to clean up.
 * @par TestCase_Expected_Result
 * 1.Concurrent join returns ALREADY_JOIN; detach after reaping returns NOT_CREATED; no resource leak.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosTask310(VOID)
{
    TEST_ADD_CASE("ItLosTask310", TestCase, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
}
