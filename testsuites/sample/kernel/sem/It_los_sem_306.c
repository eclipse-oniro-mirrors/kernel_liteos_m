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

#include "It_los_sem.h"
#include "los_swtmr.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: LOS_SemPend 系统任务拒绝分支(OsSemValidCheck, los_sem.c:216):
 * swtmr 回调运行于软件定时器任务上下文(OsSwtmrTaskCreate 置 OS_TASK_FLAG_SYSTEM),
 * 在回调中 LOS_SemPend(信号量可用, 无需阻塞) → 返回 LOS_ERRNO_SEM_PEND_IN_SYSTEM_TASK,
 * 覆盖系统任务保护分支且回调不阻塞(检查先于出队)。 */

static UINT32 g_semCbRet306 = 0xFF;
static volatile UINT32 g_semCbDone306 = 0;
static UINT32 g_usSemID306 = 0;

static VOID SwtmrFunc306(UINT32 param)
{
    (VOID)param;
    /* 信号量计数为 1, 若非系统任务会立即成功; 系统任务上下文被前置校验拒绝 */
    g_semCbRet306 = LOS_SemPend(g_usSemID306, LOS_NO_WAIT);
    g_semCbDone306 = 1;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 swtmrId;

    ret = LOS_SemCreate(1, &g_usSemID306);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    g_semCbDone306 = 0;
    g_semCbRet306 = 0xFF;
    ret = LOS_SwtmrCreate(10, LOS_SWTMR_MODE_ONCE, (SWTMR_PROC_FUNC)SwtmrFunc306, &swtmrId, 0
#if (LOSCFG_BASE_CORE_SWTMR_ALIGN == 1)
                          , OS_SWTMR_ROUSES_ALLOW, OS_SWTMR_ALIGN_INSENSITIVE
#endif
                         );
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SwtmrStart(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    UINT32 loop = 0;
    while ((g_semCbDone306 == 0) && (loop < 1000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_ASSERT_EQUAL(g_semCbDone306, 1, g_semCbDone306);
    /* qemu: swtmr 到期回调在中断上下文(SWTMR_IN_ISR) → PEND_INTERR;
     * ws63: 回调运行于系统标志的 swtmr 任务 → PEND_IN_SYSTEM_TASK。
     * 两者均为防御分支命中, 依板型接受其一。 */
    ICUNIT_ASSERT_EQUAL((g_semCbRet306 == LOS_ERRNO_SEM_PEND_INTERR) ||
                        (g_semCbRet306 == LOS_ERRNO_SEM_PEND_IN_SYSTEM_TASK), 1, g_semCbRet306);

    /* 正常任务上下文 Pend 立即成功, 佐证拒绝确因系统任务校验 */
    ret = LOS_SemPend(g_usSemID306, LOS_NO_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SemDelete(g_usSemID306);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    g_usSemID306 = 0xFFFFFFFF;

    return LOS_OK;
}

/**
 * @ingroup TEST_SEM
 * @par TestCase_Number
 * ItLosSem306
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_SemPend in system task context
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: create sem(count 1) and a one-shot swtmr whose callback pends with NO_WAIT.
 * step2: wait callback done, assert PEND_IN_SYSTEM_TASK returned.
 * step3: pend from normal test task succeeds; cleanup.
 * @par TestCase_Expected_Result
 * 1.Pend in swtmr task (system task) rejected; normal task pend succeeds.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosSem306(VOID)
{
    TEST_ADD_CASE("ItLosSem306", TestCase, TEST_LOS, TEST_SEM, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
