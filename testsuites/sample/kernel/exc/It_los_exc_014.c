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
#include "los_debug.h" /* OsDoExcHook/EXC_INTERRUPT, It_los_exc_011 直调先例 */

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#ifdef LOSCFG_PLATFORM_EXC
/* ws63 侧 exchook 公共头目录在编译包含路径(与 qemu 侧不同, 见 It_los_exc_011
 * 注释), 直接包含获取 OsExcRegister 原型/ExcInfoType/MAX_EXC_MEM_SIZE 与
 * 跨架构类型宏 EXC_CONTEXT_TYPE(RISC-V: LosExcContext, ARM: EXC_CONTEXT_S) */
#include "los_exc_info.h" /* OsExcRegister 拒绝分支 266-267 */

/* exchook 内部落盘指针/缓冲无头文件声明, 按工程先例(It_los_exc_011.c:38-39
 * 的 extern 声明方式)引出: 定义于 kernel/extended/exchook/los_exc_info.c:54-56 */
extern VOID *g_excContent;      /* 落盘游标 */
extern UINT8 g_excMsgArray[];   /* 异常信息落盘缓冲(大小 MAX_EXC_MEM_SIZE) */

/* 用例简要描述: 中断异常钩子非破坏性落盘与注册拒绝
 * 1) 保存 g_excInfo.context 原值(无条件 EXIT 恢复);
 * 2) 构造 g_excInfo.context 指向静态 EXC_CONTEXT_TYPE 缓冲(否则 OsExcContentGet
 *    第二段 memcpy_s 不到达, los_exc_info.c:191-192);
 * 3) OsDoExcHook(EXC_INTERRUPT)(It_los_exc_011 直调先例) 非破坏性触发启动期
 *    注册的 OsExcMsgDump(los_exc_info.c:280-299) -> dump 链 6 函数:
 *    OsExcContentGet/OsExcHookFunc/OsExcTaskMsgGet/OsExcQueueMsgGet/
 *    OsExcMemMsgGet 仅向 g_excMsgArray 写内存, 无 CPU 异常;
 *    断言无崩溃 + g_excMsgArray 首部 totalLen 字段 == MAX_EXC_MEM_SIZE
 *    (los_exc_info.c:287 首写字段);
 * 4) OsExcRegister(OS_EXC_TYPE_MAX, fn, NULL) 与 OsExcRegister(合法type, NULL,
 *    NULL) 均走 PRINT_ERR 拒绝分支(los_exc_info.c:266-267, 覆盖目标);
 * 5) 再次触发 dump(不手动复位游标): 验证 OsExcMsgDump 入口复位修复生效 ——
 *    第二次 dump 不越界, totalLen 仍正确 -> 拒绝注册未污染 g_excArray 注册表;
 * 6) EXIT 恢复: g_excInfo.context 恢复原值(游标由内核复位, 无需用例处理)。 */
/* 静态异常上下文缓冲(类型随架构: RISC-V LosExcContext / ARM EXC_CONTEXT_S) */
static EXC_CONTEXT_TYPE g_exc014Ctx;

static UINT32 Exc014SaveCb(UINT32 type, VOID *arg)
{
    /* 仅满足 OsExcRegister 原型, 越界 type 注册被拒绝, 本回调不会执行 */
    (VOID)type;
    (VOID)arg;
    return LOS_OK;
}

static UINT32 Testcase(VOID)
{
    EXC_CONTEXT_TYPE *origCtx = g_excInfo.context;
    UINT32 totalLen;

    /* 构造 context 指向静态缓冲(使 OsExcContentGet 两段 memcpy 均到达) */
    g_excInfo.context = &g_exc014Ctx;

    /* 3 非破坏性触发中断异常 dump 链: 仅写 g_excMsgArray, 无 CPU 异常 */
    OsDoExcHook(EXC_INTERRUPT);

    /* 断言无崩溃(执行到此即证明) + 首部 totalLen == MAX_EXC_MEM_SIZE */
    totalLen = *(volatile UINT32 *)g_excMsgArray;
    ICUNIT_GOTO_EQUAL(totalLen, MAX_EXC_MEM_SIZE, totalLen, EXIT);

    /* 4 越界 type / NULL func 均走 PRINT_ERR 拒绝分支(los_exc_info.c:266-267) */
    OsExcRegister(OS_EXC_TYPE_MAX, Exc014SaveCb, NULL);
    OsExcRegister(OS_EXC_TYPE_TSK, NULL, NULL);

    /* 5 不手动复位游标, 再次触发 dump: 验证 OsExcMsgDump 入口复位修复生效 ——
     *    第二次 dump 不越界, totalLen 仍正确 -> 拒绝注册未污染 g_excArray */
    OsDoExcHook(EXC_INTERRUPT);
    totalLen = *(volatile UINT32 *)g_excMsgArray;
    ICUNIT_GOTO_EQUAL(totalLen, MAX_EXC_MEM_SIZE, totalLen, EXIT);

EXIT:
    /* 6 仅恢复 context(游标由内核 OsExcMsgDump 入口复位, 无需用例处理) */
    g_excInfo.context = origCtx;
    return LOS_OK;
}
#endif /* LOSCFG_PLATFORM_EXC */

/**
 * @ingroup TEST_EXC
 * @par TestCase_Number
 * ItLosExc014
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Non-destructive interrupt-exc dump chain via OsDoExcHook + OsExcRegister rejects
 * @par TestCase_Pretreatment_Condition
 * LOSCFG_PLATFORM_EXC enabled (ws63); It_los_exc_011 direct-call precedent.
 * @par TestCase_Test_Steps
 * step1: save g_excInfo.context, point context to a static buffer so that
 *        OsExcContentGet's two memcpy_s both reach their targets.
 * step2: OsDoExcHook(EXC_INTERRUPT) triggers the startup-registered OsExcMsgDump:
 *        the 6-function dump chain writes g_excMsgArray only, no real fault.
 * step3: assert first-field totalLen == MAX_EXC_MEM_SIZE (los_exc_info.c:287).
 * step4: OsExcRegister with out-of-range type / NULL func -> PRINT_ERR reject
 *        (los_exc_info.c:266-267).
 * step5: trigger dump again WITHOUT manually resetting g_excContent: verifies the
 *        OsExcMsgDump entry-point cursor reset fix — second dump must not overflow
 *        g_excMsgArray and totalLen must stay correct -> g_excArray unpolluted.
 * step6: restore g_excInfo.context only (cursor now reset by the kernel).
 * @par TestCase_Expected_Result
 * 1.no crash, dump chain completes in-task.
 * 2.totalLen field correct after each dump, including the second dump that
 *   relies on the kernel-side cursor reset (no manual reset in the case).
 * 3.rejected registrations do not alter the dump output.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * Verifies the fix for OsExcMsgDump not resetting g_excContent: the dump entry
 * now resets the cursor to g_excMsgArray, so a second dump no longer writes past
 * g_excMsgArray + MAX_EXC_MEM_SIZE. The case no longer saves/restores the cursor.
 */
VOID ItLosExc014(VOID)
{
#ifdef LOSCFG_PLATFORM_EXC
    TEST_ADD_CASE("ItLosExc014", Testcase, TEST_LOS, TEST_EXC, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
