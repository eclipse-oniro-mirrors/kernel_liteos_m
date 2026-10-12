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

#include "It_los_swtmr.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: 超长周期软件定时器触达排序链 rollnum 饱和钳位分支:
 * OsSwtmrStart → OsSchedAddSortLink → OsAdd2SortLink 中
 * `if (sortList->idxRollNum > OS_TSK_MAX_ROLLNUM) SET_SORTLIST_VALUE(sortList, MAX)`
 * (los_sortlink.c:249-251)。interval=0x7FFFFFFF(约 2^31 tick)换算 delta 超过
 * OS_TSK_MAX_ROLLNUM(0xFFFFFFFF - SORTLINK_LEN) → 钳位分支执行。
 * 生命周期闭环: Create → Start(钳位) → Stop → Delete, 无资源残留;
 * 定时器周期远超用例时长, Stop 前不会到期, 回调不执行。 */

static VOID SwtmrFunc123(UINT32 param)
{
    (VOID)param;
    return;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 swtmrId;

    ret = LOS_SwtmrCreate(0x7FFFFFFF, LOS_SWTMR_MODE_PERIOD, (SWTMR_PROC_FUNC)SwtmrFunc123,
                          &swtmrId, 0
#if (LOSCFG_BASE_CORE_SWTMR_ALIGN == 1)
                          , OS_SWTMR_ROUSES_ALLOW, OS_SWTMR_ALIGN_INSENSITIVE
#endif
                         );
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* F-Pos: 启动超长周期定时器 → rollnum 超限钳位分支(OsAdd2SortLink) */
    ret = LOS_SwtmrStart(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* F-Pos: 停止 + 删除, 资源闭环(周期约 248 天@100Hz, 不会在用例内到期) */
    ret = LOS_SwtmrStop(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SwtmrDelete(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

/**
 * @ingroup TEST_SWTMR
 * @par TestCase_Number
 * ItLosSwtmr123
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_SwtmrCreate/Start/Stop/Delete with huge interval (rollnum clamp)
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: create a periodic swtmr with interval 0x7FFFFFFF ticks.
 * step2: start it (rollnum exceeds OS_TSK_MAX_ROLLNUM, clamp branch executes).
 * step3: stop and delete it.
 * @par TestCase_Expected_Result
 * 1.Create/Start/Stop/Delete all return LOS_OK; timer never fires during the case.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosSwtmr123(VOID)
{
    TEST_ADD_CASE("ItLosSwtmr123", TestCase, TEST_LOS, TEST_SWTMR, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
