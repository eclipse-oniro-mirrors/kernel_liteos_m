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
#include "los_debug.h" /* EXC_TYPE/ExcHookFn/OsDoExcHook, It_los_exc_011 同款先例 */

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* exchook 头目录不在 testsuites include 路径(qemu 侧),按工程先例 extern 声明
   (实现: kernel/extended/exchook/los_exchook.c; 先例: It_los_exc_011.c:38-39) */
extern UINT32 LOS_RegExcHook(EXC_TYPE excType, ExcHookFn excHookFn);
extern UINT32 LOS_UnRegExcHook(EXC_TYPE excType, ExcHookFn excHookFn);

#ifdef LOSCFG_PLATFORM_EXC

/* 用例简要描述: 钩子链满载注册与非头节点注销回接
 * 1) 同类型(EXC_ASSERT)注册 2 个钩子 A/B: OsDoExcHook(EXC_ASSERT) 反序派发
 *    (注册序 A,B -> 派发序 B,A), 计数 g_cntA/g_cntB 各 1;
 * 2) 注销先注册的 A(链上为非头节点, 头为 B): 命中 los_exchook.c:123-124
 *    preNode 回接分支(本用例覆盖目标); 再派发仅剩 B(g_cntA 不变, g_cntB 加 1);
 * 3) 循环注册哑钩子至节点用满: LOSCFG_BASE_EXC_HOOK_LIMIT 默认 16
 *    (los_exchook.c:35-37, 含启动期 OsExcMsgDumpInit 已注册的 OsExcMsgDump
 *    占用, 故不硬编码次数, 有界循环直至返回值非 LOS_OK), 断言
 *    LOS_ERRNO_SYS_HOOK_IS_FULL(los_exchook.c:100-101, 覆盖目标);
 * 4) 恢复(无条件 EXIT): 按实际注册次数逐个注销哑钩子(UnReg 对同号节点单次
 *    移除一个), 注销 A/B, 再注册/注销一次验证节点池已恢复可用。 */
static UINT32 g_cntA013 = 0;
static UINT32 g_cntB013 = 0;

/* 有界上界: 覆盖 LOSCFG_BASE_EXC_HOOK_LIMIT 任何合理配置(默认 16) */
#define EXC013_REG_LOOP_MAX 65

static VOID Exc013HookA(EXC_TYPE excType)
{
    (VOID)excType;
    g_cntA013++;
}

static VOID Exc013HookB(EXC_TYPE excType)
{
    (VOID)excType;
    g_cntB013++;
}

static VOID Exc013DummyHook(EXC_TYPE excType)
{
    (VOID)excType;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 i;
    UINT32 fillCnt = 0;

    g_cntA013 = 0;
    g_cntB013 = 0;

    /* 1 同类型注册 2 个钩子: 链为 B(头) -> A */
    ret = LOS_RegExcHook(EXC_ASSERT, Exc013HookA);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_RegExcHook(EXC_ASSERT, Exc013HookB);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 反序派发: B 后 A, 两个钩子各计数 1 */
    OsDoExcHook(EXC_ASSERT);
    ICUNIT_GOTO_EQUAL(g_cntA013, 1, g_cntA013, EXIT);
    ICUNIT_GOTO_EQUAL(g_cntB013, 1, g_cntB013, EXIT);

    /* 2 注销非头节点 A(命中 los_exchook.c:123-124 preNode 回接), 再派发仅剩 B */
    ret = LOS_UnRegExcHook(EXC_ASSERT, Exc013HookA);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    OsDoExcHook(EXC_ASSERT);
    ICUNIT_GOTO_EQUAL(g_cntA013, 1, g_cntA013, EXIT); /* A 不再派发 */
    ICUNIT_GOTO_EQUAL(g_cntB013, 2, g_cntB013, EXIT);

    /* 3 循环注册哑钩子至节点用满(启动期 OsExcMsgDump 已占 1 个, 有界探测) */
    for (i = 0; i < EXC013_REG_LOOP_MAX; i++) {
        ret = LOS_RegExcHook(EXC_ASSERT, Exc013DummyHook);
        if (ret != LOS_OK) {
            break;
        }
        fillCnt++;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_SYS_HOOK_IS_FULL, ret, EXIT);

    /* 4 恢复: 按实际注册次数逐个注销哑钩子, 再注销 B */
    for (i = 0; i < fillCnt; i++) {
        ret = LOS_UnRegExcHook(EXC_ASSERT, Exc013DummyHook);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    }
    ret = LOS_UnRegExcHook(EXC_ASSERT, Exc013HookB);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 全部注销恢复后: 节点池可用, 注册/注销一次回归验证 */
    ret = LOS_RegExcHook(EXC_ASSERT, Exc013DummyHook);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_UnRegExcHook(EXC_ASSERT, Exc013DummyHook);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    return LOS_OK;

EXIT:
    /* 无条件清理: 哑钩子按实际注册次数注销, A/B 注销(不存在时为无害 no-op) */
    for (i = 0; i < fillCnt; i++) {
        (VOID)LOS_UnRegExcHook(EXC_ASSERT, Exc013DummyHook);
    }
    (VOID)LOS_UnRegExcHook(EXC_ASSERT, Exc013HookA);
    (VOID)LOS_UnRegExcHook(EXC_ASSERT, Exc013HookB);
    return LOS_OK;
}
#endif /* LOSCFG_PLATFORM_EXC */

/**
 * @ingroup TEST_EXC
 * @par TestCase_Number
 * ItLosExc013
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test LOS_RegExcHook/LOS_UnRegExcHook full-pool and non-head-node removal
 * @par TestCase_Pretreatment_Condition
 * LOSCFG_PLATFORM_EXC enabled (ws63).
 * @par TestCase_Test_Steps
 * step1: register two hooks of the same type, dispatch via OsDoExcHook (reverse order).
 * step2: unregister the first-registered (non-head) hook -> preNode relink
 *        (los_exchook.c:124), dispatch again only the remaining one.
 * step3: loop-register dummy hooks until the node pool is full (startup-registered
 *        OsExcMsgDump counted), assert LOS_ERRNO_SYS_HOOK_IS_FULL (los_exchook.c:100-101).
 * step4: drain all dummies and hooks, verify the pool is reusable again.
 * @par TestCase_Expected_Result
 * 1.non-head unregister leaves only the other hook dispatched.
 * 2.registering beyond the limit returns LOS_ERRNO_SYS_HOOK_IS_FULL.
 * 3.after cleanup, register/unregister works again (LOS_OK).
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */
VOID ItLosExc013(VOID)
{
#ifdef LOSCFG_PLATFORM_EXC
    TEST_ADD_CASE("ItLosExc013", Testcase, TEST_LOS, TEST_EXC, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
