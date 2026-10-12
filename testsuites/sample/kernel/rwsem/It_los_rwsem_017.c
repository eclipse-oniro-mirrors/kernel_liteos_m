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

#include "It_los_rwsem.h"
#include "los_swtmr.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: LOS_RwsemPendRead/LOS_RwsemPendWrite 无竞争快路径 + 系统任务告警分支:
 * 1) F-Pos: 新建读写锁后直接 PendRead(无等待者, count>=初始值) → 快路径 count++
 *    (los_rwsem.c:216), PostRead 归还;
 * 2) F-Pos: 直接 PendWrite(count==初始值) → 快路径 count--(los_rwsem.c:259),
 *    PostWrite 归还;
 * 3) F-Pos: 在 swtmr 回调(系统任务上下文, OsSwtmrTaskCreate 置 OS_TASK_FLAG_SYSTEM)
 *    中 PendRead → RwsemPendCheck 系统任务告警分支(los_rwsem.c:167, 仅告警不拒绝),
 *    无竞争场景不阻塞, 回调内完成 PostRead 归还;
 * 4) 收尾删除读写锁与定时器, 无资源残留。 */

static UINT32 g_rwsemCbDone017 = 0;
static UINT32 g_rwsemH017 = 0;

static VOID SwtmrFunc017(UINT32 param)
{
    UINT32 ret;
    (VOID)param;
    /* 系统任务上下文 PendRead: 触发告警分支, 无竞争立即成功 */
    ret = LOS_RwsemPendRead(g_rwsemH017, LOS_NO_WAIT);
    if (ret == LOS_OK) {
        (VOID)LOS_RwsemPostRead(g_rwsemH017);
    }
    g_rwsemCbDone017 = 1;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 swtmrId;

    ret = LOS_RwsemCreate(&g_rwsemH017);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 1 F-Pos: 无竞争 PendRead 快路径 */
    ret = LOS_RwsemPendRead(g_rwsemH017, LOS_NO_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_RwsemPendRead(g_rwsemH017, LOS_NO_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_RwsemPostRead(g_rwsemH017);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_RwsemPostRead(g_rwsemH017);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 2 F-Pos: 无竞争 PendWrite 快路径 */
    ret = LOS_RwsemPendWrite(g_rwsemH017, LOS_NO_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_RwsemPostWrite(g_rwsemH017);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 3 F-Pos: swtmr 回调(系统任务)中 PendRead → 告警分支 */
    g_rwsemCbDone017 = 0;
    ret = LOS_SwtmrCreate(10, LOS_SWTMR_MODE_ONCE, (SWTMR_PROC_FUNC)SwtmrFunc017, &swtmrId, 0
#if (LOSCFG_BASE_CORE_SWTMR_ALIGN == 1)
                          , OS_SWTMR_ROUSES_ALLOW, OS_SWTMR_ALIGN_INSENSITIVE
#endif
                         );
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SwtmrStart(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    UINT32 loop = 0;
    while ((g_rwsemCbDone017 == 0) && (loop < 1000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_ASSERT_EQUAL(g_rwsemCbDone017, 1, g_rwsemCbDone017);

    /* 一次性定时器到期自删, 清理锁 */
    ret = LOS_RwsemDelete(g_rwsemH017);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

/**
 * @ingroup TEST_RWSEM
 * @par TestCase_Number
 * ItLosRwsem017
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_RwsemPendRead/LOS_RwsemPendWrite fast paths and system-task warn branch
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: uncontended PendRead twice + PostRead twice.
 * step2: uncontended PendWrite + PostWrite.
 * step3: PendRead inside swtmr callback (system task context).
 * @par TestCase_Expected_Result
 * 1.Fast paths return LOS_OK; system-task warning branch exercised without rejection.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosRwsem017(VOID)
{
    TEST_ADD_CASE("ItLosRwsem017", TestCase, TEST_LOS, TEST_RWSEM, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
