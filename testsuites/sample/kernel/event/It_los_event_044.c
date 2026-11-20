/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the following conditions:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "It_los_event.h"

/* los_event.h:336 的 LOS_EventCondWrite 声明被 #ifdef LOSCFG_COMPAT_POSIX 门控, 该宏
 * 在主线树无任何 Kconfig/板级 config 定义源(三板 config.h 实证); 实现位于
 * los_event.c:379, 仅受 LOSCFG_BASE_IPC_EVENT 保护, 全板无条件编译(无调用者时被
 * gc 丢弃, 双板 map 中 .text.LOS_EventCondWrite 节存在佐证)。按工程先例
 * (It_los_exc_011.c:38-39 exchook 头不在 include 路径时用例内 extern 声明)在此
 * extern 声明; 上游修复声明门控后应移除本 extern。 */
extern UINT32 LOS_EventCondWrite(PEVENT_CB_S eventCB);

static UINT32 g_condReadRet = 0xFFFFFFFF;

static VOID TaskF01(VOID)
{
    UINT32 ret;

    g_testCount++;
    /* 高优先级任务先运行: 置计数后阻塞于事件读, 被 CondWrite 全唤醒 */
    ret = LOS_EventRead(&g_pevent, 0x1111, LOS_WAITMODE_AND, LOS_WAIT_FOREVER);
    /* CondWrite 不置事件位 -> 二次轮询未命中: OsEventRead 尾部 OsEventPoll 返回 0
     * (los_event.c:154), LOS_EventRead 原样返回 0(非错误码, 契约见设计说明书 §4.7) */
    g_condReadRet = ret;
    g_testCount++;

    ret = LOS_TaskDelete(g_testTaskID01);
    ICUNIT_ASSERT_EQUAL_VOID(ret, LOS_OK, ret);
    return;
}

/* 用例简要描述: 条件写接口契约一揽子:CondWrite 不置位/无 mask 过滤全唤醒/被唤醒者 LOS_EventRead 二次轮询未命中返回 0 */
static UINT32 Testcase(VOID)
{
    UINT32 ret;
    TSK_INIT_PARAM_S task1;

    (void)memset_s(&task1, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    task1.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF01;
    task1.pcName = "EventCondTsk1";
    task1.uwStackSize = TASK_STACK_SIZE_TEST;
    task1.usTaskPrio = TASK_PRIO_TEST - 2; // 2, set new task priority, it is higher than the test task.
    task1.uwResved = LOS_TASK_STATUS_DETACHED;

    g_testCount = 0;
    g_condReadRet = 0xFFFFFFFF;
    g_pevent.uwEventID = 0;
    LOS_EventInit(&g_pevent);

#ifdef LOSCFG_KERNEL_SMP
    task1.usCpuAffiMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());
#endif

    /* F-Neg: NULL 控制块 -> LOS_ERRNO_EVENT_PTR_NULL (los_event.c:385) */
    ret = LOS_EventCondWrite(NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_EVENT_PTR_NULL, ret);

    /* F-Pos: 空阻塞表 CondWrite -> LOS_OK, exitFlag==0 不走调度路径; 事件位不被置位 */
    ret = LOS_EventCondWrite(&g_pevent);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(g_pevent.uwEventID, 0, g_pevent.uwEventID);

    ret = LOS_TaskCreate(&g_testTaskID01, &task1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 高优任务先于主任务运行: 到达阻塞点后计数==1 且唯一阻塞(确定性, 无需 delay 同步) */
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT);

    /* F-Pos: 全唤醒无 mask 过滤: exitFlag==1 -> LOS_MpSchedule + LOS_Schedule,
     * CondWrite 返回前高优任务已恢复并完成(计数==2) */
    ret = LOS_EventCondWrite(&g_pevent);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 被唤醒者 LOS_EventRead 返回 0 且事件位仍为 0(不置位契约) */
    ICUNIT_GOTO_EQUAL(g_testCount, 2, g_testCount, EXIT);
    ICUNIT_GOTO_EQUAL(g_condReadRet, 0, g_condReadRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_pevent.uwEventID, 0, g_pevent.uwEventID, EXIT);

    ret = LOS_EventDestroy(&g_pevent);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    return LOS_OK;

EXIT:
    /* 清理(无条件): 先唤醒可能阻塞中的任务, 再删任务/销毁事件(失败码忽略, 主断言已失败) */
    (VOID)LOS_EventCondWrite(&g_pevent);
    (VOID)LOS_TaskDelete(g_testTaskID01);
    (VOID)LOS_EventDestroy(&g_pevent);
    return LOS_OK;
}

/**
 * @ingroup TEST_FN_KERNEL_EVENT
 * @par TestCase_Number
 * ItLosEvent044
 * @par TestCase_TestCase_Type
 * Function test
 * @brief LOS_EventCondWrite conditional-write contract (no bit-set, wake-all, woken-reader re-poll miss)
 * @par TestCase_Pretreatment_Condition
 * task context, event module enabled.
 * @par TestCase_Test_Steps
 * step1: LOS_EventCondWrite(NULL) expects LOS_ERRNO_EVENT_PTR_NULL.
 * step2: CondWrite on initialized event with empty pend list expects LOS_OK and uwEventID unchanged.
 * step3: higher-priority task blocks in LOS_EventRead(0x1111, AND, FOREVER); CondWrite wakes it.
 * @par TestCase_Expected_Result
 * 1.NULL arg rejected with LOS_ERRNO_EVENT_PTR_NULL.
 * 2.Empty-list CondWrite returns LOS_OK and sets no event bits.
 * 3.Woken reader's LOS_EventRead returns 0 (re-poll miss, not an error) and uwEventID keeps 0.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * Declaration extern'd in-file: los_event.h:336 is gated by dead macro LOSCFG_COMPAT_POSIX.
 */

VOID ItLosEvent044(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosEvent044", Testcase, TEST_LOS, TEST_FN_KERNEL_EVENT, TEST_LEVEL0, TEST_FUNCTION);
}
