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

/* 用例简要描述: LOS_TaskDelete/LOS_TaskResume/LOS_TaskPriSet 均返 LOS_ERRNO_TSK_IS_ZOMBIE; Join 回收返 LOS_OK(zombie 态负向操作族) */
static VOID TaskF309(UINT32 arg)
{
    (VOID)arg;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 taskID = 0xFFFFFFFF;
    UINT32 status = 0;
    UINT32 loop;
    UINTPTR retval = 0;
    TSK_INIT_PARAM_S taskParam = {0};

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF309;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "Tsk309Z";
    taskParam.usTaskPrio = TASK_PRIO_TEST - 2; /* 高于本任务: 创建即抢占运行并立即退出成 zombie */
    taskParam.uwResved = LOS_TASK_ATTR_JOINABLE;

    ret = LOS_TaskCreate(&taskID, &taskParam);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 有界轮询至 ZOMBIE(joinable 任务退出后等待 Join 回收,不入 free 列表) */
    loop = 0;
    do {
        ret = LOS_TaskStatusGet(taskID, &status);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
        if (status & OS_TASK_STATUS_ZOMBIE) {
            break;
        }
        loop++;
        (VOID)LOS_TaskDelay(1);
    } while (loop < 1000);
    ICUNIT_GOTO_EQUAL((status & OS_TASK_STATUS_ZOMBIE), OS_TASK_STATUS_ZOMBIE, status, EXIT);

    /* F-Inv: zombie 态 LOS_TaskDelete → LOS_ERRNO_TSK_IS_ZOMBIE (los_task.c:1062) */
    ret = LOS_TaskDelete(taskID);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_TSK_IS_ZOMBIE, ret, EXIT);

    /* F-Inv: zombie 态 LOS_TaskResume → LOS_ERRNO_TSK_IS_ZOMBIE (los_task.c:1169) */
    ret = LOS_TaskResume(taskID);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_TSK_IS_ZOMBIE, ret, EXIT);

    /* F-Inv: zombie 态 LOS_TaskPriSet → LOS_ERRNO_TSK_IS_ZOMBIE (los_task.c:1310) */
    ret = LOS_TaskPriSet(taskID, TASK_PRIO_TEST - 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_TSK_IS_ZOMBIE, ret, EXIT);

    /* 清理: Join 回收 zombie → LOS_OK */
    ret = LOS_TaskJoin(taskID, &retval);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    return LOS_OK;

EXIT:
    /* 兜底回收: 任务若仍存在(运行态/zombie), Join 等其退出并回收; 已回收则返错误码忽略 */
    if (taskID != 0xFFFFFFFF) {
        (VOID)LOS_TaskJoin(taskID, &retval);
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_TASK
 * @par TestCase_Number
 * ItLosTask309
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test negative operations on a zombie task
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: create a joinable task which exits immediately and becomes zombie.
 * step2: LOS_TaskDelete/LOS_TaskResume/LOS_TaskPriSet on it, expect LOS_ERRNO_TSK_IS_ZOMBIE.
 * step3: LOS_TaskJoin reaps the zombie, expect LOS_OK.
 * @par TestCase_Expected_Result
 * 1.All zombie-state rejections return LOS_ERRNO_TSK_IS_ZOMBIE; Join returns LOS_OK.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosTask309(VOID)
{
    TEST_ADD_CASE("ItLosTask309", TestCase, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
}
