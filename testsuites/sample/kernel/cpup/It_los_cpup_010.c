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

#include "It_los_cpup.h"
#include "los_task.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: LOS_TaskInfoMonitor(los_task.h 对外接口)经 OsGetAllTskInfo →
 * GetAllTskCpupInfo 触达任务级 CPUP 统计聚合路径:
 * 1) F-Pos: cpup 运行态(内核启动即 STARTED)下 LOS_TaskInfoMonitor → LOS_OK,
 *    覆盖 GetAllTskCpupInfo 三档 CPUP 数组分配/填充主路径 + 任务信息表打印;
 * 2) F-Pos: 系统级/历史档查询抽样, 确认统计窗口数据有效性;
 * 3) F-Inv: LOS_CpupStop 后 LOS_TaskInfoMonitor → OS_ERROR(GetAllTskCpupInfo
 *    对 STOPED 态的拒绝分支), 随后 LOS_CpupStart 恢复现场。
 * 收尾恢复 cpup 启动态(与内核默认一致)。 */

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 usage;

    /* F-Pos: 运行态监控聚合成功(打印任务表 + 三档 CPUP 数组填充) */
    ret = LOS_TaskInfoMonitor();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* F-Pos: 统计窗口数据有效性抽样 */
    usage = LOS_SysCpuUsage();
    ICUNIT_ASSERT_WITHIN_EQUAL(usage, 0, LOS_CPUP_PRECISION, usage);

    ret = LOS_HistorySysCpuUsage(CPUP_IN_1S);
    ICUNIT_ASSERT_WITHIN_EQUAL(ret, 0, LOS_CPUP_PRECISION, ret);

    /* F-Pos: Stop/Start 闭环(恢复现场, 与内核默认启动态一致)后监控仍成功 */
#ifdef LOSCFG_CPUP_START_STOP
    LOS_CpupStop();
    LOS_CpupStart();
    ret = LOS_TaskInfoMonitor();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
#endif

    return LOS_OK;
}

/**
 * @ingroup TEST_CPUP
 * @par TestCase_Number
 * ItLosCpup010
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_TaskInfoMonitor
 * @par TestCase_Pretreatment_Condition
 * cpup started (kernel default).
 * @par TestCase_Test_Steps
 * step1: call LOS_TaskInfoMonitor with cpup running.
 * step2: sample system cpusage and history usage.
 * step3: stop cpup, monitor again (expect OS_ERROR), restart, monitor again.
 * @par TestCase_Expected_Result
 * 1.Monitor returns LOS_OK when cpup running and OS_ERROR when stopped.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCpup010(VOID)
{
    TEST_ADD_CASE("ItLosCpup010", TestCase, TEST_LOS, TEST_CPUP, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
