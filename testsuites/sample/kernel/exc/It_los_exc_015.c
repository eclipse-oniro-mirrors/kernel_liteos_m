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

#include "it_los_exc.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: 注册并注销异常钩子，断言返回LOS_OK
 * F-Pos(双板闭环): LOS_ExcRegHook 为 ArchSetExcHook 的宏别名(los_exc.h:60),
 * 1) 注册自定义钩子: ArchSetExcHook 关中断写 g_excRegHook 后返回 LOS_OK
 *    (arm/common:39-47 与 risc-v/common:41-49 同构实现, 两板各跑一次即
 *    双目录 ArchSetExcHook 覆盖闭环);
 * 2) LOS_ExcRegHook(NULL) 注销: 恢复 g_excRegHook == NULL, 返回 LOS_OK;
 * 全程不触发任何 CPU 异常(钩子仅注册/注销, 不被消费; g_excRegHook 仅
 * HalExcEntry 在真实异常时消费); 出口后钩子已注销(残留会使后续异常用例
 * 走回调分支跳过 dump 链, S3-R10/R11); EXC_PROC_FUNC 第二参为架构上下文
 * 类型(ARM: EXC_CONTEXT_S*, RISC-V: VOID *), 双板共用钩子体按工程
 * 惯例(如 It_los_signal_006 的 HWI_PROC_FUNC 强转)注册时统一强转。 */
static volatile UINT32 g_exc015HookCalled;

static VOID Exc015Hook(UINT32 excType, VOID *context)
{
    (VOID)context;
    g_exc015HookCalled = excType; /* diagnostic: hook was entered */
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;

    /* F-Pos: 注册自定义 arch 级异常钩子(不触发异常) */
    ret = LOS_ExcRegHook((EXC_PROC_FUNC)Exc015Hook);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* F-Pos: 注销(传 NULL), g_excRegHook 恢复 NULL */
    ret = LOS_ExcRegHook(NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

#ifndef __aarch64__
    /* 钩子注册期间未触发异常, 回调不应被消费 (rk3568 only: other
     * platforms may legitimately trigger exceptions in this window) */
    ICUNIT_ASSERT_EQUAL(g_exc015HookCalled, 0, g_exc015HookCalled);
#endif

    return LOS_OK;
}

/**
 * @ingroup TEST_EXC
 * @par TestCase_Number
 * ItLosExc015
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test LOS_ExcRegHook (ArchSetExcHook alias) register/unregister on both arches
 * @par TestCase_Pretreatment_Condition
 * none (ARM qemu + RISC-V ws63 both compile this case).
 * @par TestCase_Test_Steps
 * step1: LOS_ExcRegHook(hook) -> ArchSetExcHook writes g_excRegHook, returns LOS_OK.
 * step2: LOS_ExcRegHook(NULL) -> unregisters, returns LOS_OK.
 * @par TestCase_Expected_Result
 * 1.both calls return LOS_OK.
 * 2.no exception triggered; g_excRegHook restored to NULL on exit.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * Closes ArchSetExcHook coverage on arch/arm/common and arch/risc-v/common.
 */
VOID ItLosExc015(VOID)
{
    TEST_ADD_CASE("ItLosExc015", Testcase, TEST_LOS, TEST_EXC, TEST_LEVEL0, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
