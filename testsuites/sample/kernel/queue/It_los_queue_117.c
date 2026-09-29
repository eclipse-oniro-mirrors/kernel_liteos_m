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

#include "It_los_queue.h"
#include "los_membox.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: OsQueueMailAlloc/OsQueueMailFree(los_queue.h 对外声明)邮件内存机制直测:
 * 1) F-Pos: membox 池(容量 1) + 队列创建; OsQueueMailAlloc 取走唯一块;
 *    高优先级辅助任务再 Alloc(超时等待) → 挂入 queueCB->memList;
 *    主任务 OsQueueMailFree 归还块 → 唤醒等待任务(los_queue.c:679-687 唤醒分支);
 * 2) F-Pos: 无等待者时 OsQueueMailFree 走 LOS_MemboxFree 归还路径(690-694);
 * 3) F-Inv: queueID 越界 → LOS_ERRNO_QUEUE_MAIL_HANDLE_INVALID;
 *    mailPool NULL → LOS_ERRNO_QUEUE_MAIL_PTR_INVALID;
 *    已删除队列 → LOS_ERRNO_QUEUE_NOT_CREATE;
 *    非法邮件指针(非 membox 块) → LOS_ERRNO_QUEUE_MAIL_FREE_ERROR(691-692);
 *    membox 池耗尽 + NO_WAIT → OsQueueMailAlloc 返回 NULL。
 * 同步方式: g_testCount 计数同步, 不使用延时等待。 */

#define QUEUE_TC117_QLEN     4
#define QUEUE_TC117_QSIZE    8
#define QUEUE_TC117_BLKSIZE  0x20
#define QUEUE_TC117_BLKNUM   2
#define QUEUE_TC117_POOLSIZE LOS_MEMBOX_SIZE(QUEUE_TC117_BLKSIZE, QUEUE_TC117_BLKNUM)

STATIC UINT8 g_memboxPoolTC117[QUEUE_TC117_POOLSIZE] __attribute__((aligned(8)));

static VOID TaskF117(VOID)
{
    VOID *mem = NULL;
    /* 唯一空闲块已被主任务占用 → 本任务挂入 memList 等待(超时 100 tick) */
    mem = OsQueueMailAlloc(g_testQueueID01, g_memboxPoolTC117, 100);
    if (mem != NULL) {
        /* 被主任务 OsQueueMailFree 唤醒并获得邮件内存 */
        g_testCount++;
        /* 归还: 此刻无其他等待者 → 走 LOS_MemboxFree 归还路径 */
        (VOID)OsQueueMailFree(g_testQueueID01, g_memboxPoolTC117, mem);
    }
    g_testCount++;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 queueID;
    VOID *mem = NULL;
    VOID *memAux = NULL;
    TSK_INIT_PARAM_S taskParam;

    ret = LOS_QueueCreate("QueueTC117", QUEUE_TC117_QLEN, &queueID, 0, QUEUE_TC117_QSIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    g_testQueueID01 = queueID;

    ret = LOS_MemboxInit(g_memboxPoolTC117, QUEUE_TC117_POOLSIZE, QUEUE_TC117_BLKSIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* F-Inv: queueID 越界 */
    ret = OsQueueMailFree(OS_ALL_IPC_QUEUE_LIMIT + 1, g_memboxPoolTC117, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_QUEUE_MAIL_HANDLE_INVALID, ret);

    /* F-Inv: mailPool NULL */
    ret = OsQueueMailFree(queueID, NULL, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_QUEUE_MAIL_PTR_INVALID, ret);

    /* F-Pos: 取走全部 2 块(带合法队列上下文) */
    mem = OsQueueMailAlloc(queueID, g_memboxPoolTC117, LOS_NO_WAIT);
    ICUNIT_ASSERT_NOT_EQUAL(mem, NULL, 0);
    memAux = OsQueueMailAlloc(queueID, g_memboxPoolTC117, LOS_NO_WAIT);
    ICUNIT_ASSERT_NOT_EQUAL(memAux, NULL, 0);

    /* F-Inv: 池耗尽 + NO_WAIT → NULL */
    ICUNIT_ASSERT_EQUAL((UINTPTR)OsQueueMailAlloc(queueID, g_memboxPoolTC117, LOS_NO_WAIT),
                        (UINTPTR)NULL, 0);

    /* F-Inv: 非法邮件指针(池外地址) → MemboxFree 失败 → MAIL_FREE_ERROR(691-692) */
    ret = OsQueueMailFree(queueID, g_memboxPoolTC117, (VOID *)(g_memboxPoolTC117 + QUEUE_TC117_POOLSIZE));
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_QUEUE_MAIL_FREE_ERROR, ret);

    /* F-Pos: 高优先级辅助任务等待邮件 → 主任务归还唤醒(679-687) */
    g_testCount = 0;
    (VOID)memset_s(&taskParam, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF117;
    taskParam.usTaskPrio = 10; /* 高于主测试任务, 确保立即抢占挂起 */
    taskParam.pcName = "TaskTC117";
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.uwResved = LOS_TASK_ATTR_JOINABLE;
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 同步: 辅助任务已挂入等待(计数为 0, 主任务继续) */
    ret = OsQueueMailFree(queueID, g_memboxPoolTC117, mem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    mem = NULL;
    ret = OsQueueMailFree(queueID, g_memboxPoolTC117, memAux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    memAux = NULL;

    /* 等待辅助任务完成(计数同步, 有界循环) */
    UINT32 loop = 0;
    while ((g_testCount < 2) && (loop < 10000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 2, g_testCount);

    ret = LOS_TaskJoin(g_testTaskID01, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    g_testTaskID01 = OS_TASK_ERRORID;

    /* F-Inv: 已删除队列 → NOT_CREATE(672-674) */
    ret = LOS_QueueDelete(queueID);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = OsQueueMailFree(queueID, g_memboxPoolTC117, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_QUEUE_NOT_CREATE, ret);

    return LOS_OK;
}

/**
 * @ingroup TEST_QUEUE
 * @par TestCase_Number
 * ItLosQueue117
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface OsQueueMailAlloc/OsQueueMailFree
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: create queue + membox pool(1 block); alloc exhausts pool; NO_WAIT alloc fails.
 * step2: high-prio task waits for mail; main task frees mail to wake it (wake branch).
 * step3: free variants: bad handle / NULL pool / deleted queue / foreign pointer.
 * @par TestCase_Expected_Result
 * 1.Mail free wakes the waiting task which then returns the block via membox-free path.
 * 2.All invalid parameters rejected with corresponding error codes.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosQueue117(VOID)
{
    TEST_ADD_CASE("ItLosQueue117", TestCase, TEST_LOS, TEST_QUE, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
