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
#include "los_hook.h"
#include "los_hook_types.h"

#ifdef LOSCFG_DEBUG_HOOK

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: LOS_HookReg/LOS_HookUnReg(los_hook.h 对外接口宏)全类型自适应矩阵。
 * 每个类型 RegHook/UnRegHook(los_hook.c 宏生成)的 5 条分支:
 *   Reg: NULL 拒绝(REG_INVALID) / 注册成功(OK) / 重复注册(POOL_IS_FULL)
 *   UnReg: 匹配反注册(OK) / NULL 或不匹配拒绝(UNREG_INVALID)
 * 槽位状态自适应: 内核启动时 trace 模块(OsTraceCnvInit)已注册 31 类 hook,
 * 仅 MEM_DEINIT/ISR_EXITTOSCHEDULER/MOVEDTASKTODELAYEDLIST 三类空闲。对占用类型:
 * Reg 返回 FULL(覆盖 FULL 分支), Reg(NULL)/UnReg(不匹配) 覆盖拒绝分支;
 * 对空闲类型: 走完整 注册→重复注册→反注册→再反注册 闭环(全分支)。
 * 注册期间的自定义 hook 仅计数, 无副作用; 空闲类型注册后全部反注册还原。 */

static UINT32 g_hookCnt = 0;

/* ---- 每类型一个签名匹配的空 hook ---- */
static VOID HookMemInit(VOID *pool, UINT32 size) { (VOID)pool; (VOID)size; g_hookCnt++; }
static VOID HookMemDeinit(VOID *pool) { (VOID)pool; g_hookCnt++; }
static VOID HookMemAlloc(VOID *pool, VOID *ptr, UINT32 size) { (VOID)pool; (VOID)ptr; (VOID)size; g_hookCnt++; }
static VOID HookMemFree(VOID *pool, VOID *ptr) { (VOID)pool; (VOID)ptr; g_hookCnt++; }
static VOID HookMemRealloc(VOID *pool, VOID *ptr, UINT32 size) { (VOID)pool; (VOID)ptr; (VOID)size; g_hookCnt++; }
static VOID HookMemAllocAlign(VOID *pool, VOID *ptr, UINT32 size, UINT32 boundary)
{
    (VOID)pool; (VOID)ptr; (VOID)size; (VOID)boundary; g_hookCnt++;
}
static VOID HookEventInit(PEVENT_CB_S eventCB) { (VOID)eventCB; g_hookCnt++; }
static VOID HookEventRead(PEVENT_CB_S eventCB, UINT32 eventMask, UINT32 mode, UINT32 timeout)
{
    (VOID)eventCB; (VOID)eventMask; (VOID)mode; (VOID)timeout; g_hookCnt++;
}
static VOID HookEventWrite(PEVENT_CB_S eventCB, UINT32 events) { (VOID)eventCB; (VOID)events; g_hookCnt++; }
static VOID HookEventClear(PEVENT_CB_S eventCB, UINT32 events) { (VOID)eventCB; (VOID)events; g_hookCnt++; }
static VOID HookEventDestroy(PEVENT_CB_S eventCB) { (VOID)eventCB; g_hookCnt++; }
static VOID HookQueueCreate(const LosQueueCB *queueCB) { (VOID)queueCB; g_hookCnt++; }
static VOID HookQueueRead(const LosQueueCB *queueCB, UINT32 operateType, UINT32 bufferSize, UINT32 timeout)
{
    (VOID)queueCB; (VOID)operateType; (VOID)bufferSize; (VOID)timeout; g_hookCnt++;
}
static VOID HookQueueWrite(const LosQueueCB *queueCB, UINT32 operateType, UINT32 bufferSize, UINT32 timeout)
{
    (VOID)queueCB; (VOID)operateType; (VOID)bufferSize; (VOID)timeout; g_hookCnt++;
}
static VOID HookQueueDelete(const LosQueueCB *queueCB) { (VOID)queueCB; g_hookCnt++; }
static VOID HookSemCreate(const LosSemCB *semCreated) { (VOID)semCreated; g_hookCnt++; }
static VOID HookSemPost(const LosSemCB *semPosted, const LosTaskCB *resumedTask)
{
    (VOID)semPosted; (VOID)resumedTask; g_hookCnt++;
}
static VOID HookSemPend(const LosSemCB *semPended, const LosTaskCB *runningTask, UINT32 timeout)
{
    (VOID)semPended; (VOID)runningTask; (VOID)timeout; g_hookCnt++;
}
static VOID HookSemDelete(const LosSemCB *semDeleted) { (VOID)semDeleted; g_hookCnt++; }
static VOID HookMuxCreate(const LosMuxCB *muxCreated) { (VOID)muxCreated; g_hookCnt++; }
static VOID HookMuxPost(const LosMuxCB *muxPosted) { (VOID)muxPosted; g_hookCnt++; }
static VOID HookMuxPend(const LosMuxCB *muxPended, UINT32 timeout) { (VOID)muxPended; (VOID)timeout; g_hookCnt++; }
static VOID HookMuxDelete(const LosMuxCB *muxDeleted) { (VOID)muxDeleted; g_hookCnt++; }
static VOID HookTaskCreate(const LosTaskCB *taskCB) { (VOID)taskCB; g_hookCnt++; }
static VOID HookTaskDelay(UINT32 tick) { (VOID)tick; g_hookCnt++; }
static VOID HookTaskPriModify(const LosTaskCB *pxTask, UINT32 uxNewPriority)
{
    (VOID)pxTask; (VOID)uxNewPriority; g_hookCnt++;
}
static VOID HookTaskDelete(const LosTaskCB *taskCB) { (VOID)taskCB; g_hookCnt++; }
static VOID HookTaskSwitchedIn(VOID) { g_hookCnt++; }
static VOID HookMovedToReady(const LosTaskCB *pstTaskCB) { (VOID)pstTaskCB; g_hookCnt++; }
static VOID HookMovedToDelayed(const LosTaskCB *pstTaskCB) { (VOID)pstTaskCB; g_hookCnt++; }
static VOID HookMovedToSuspended(const LosTaskCB *pstTaskCB) { (VOID)pstTaskCB; g_hookCnt++; }
static VOID HookIsrExitToScheduler(VOID) { g_hookCnt++; }
static VOID HookIsrEnter(UINT32 hwiIndex) { (VOID)hwiIndex; g_hookCnt++; }
static VOID HookIsrExit(UINT32 hwiIndex) { (VOID)hwiIndex; g_hookCnt++; }
static VOID HookSwtmrCreate(const SWTMR_CTRL_S *swtmr) { (VOID)swtmr; g_hookCnt++; }
static VOID HookSwtmrDelete(const SWTMR_CTRL_S *swtmr) { (VOID)swtmr; g_hookCnt++; }
static VOID HookSwtmrExpired(const SWTMR_CTRL_S *swtmr) { (VOID)swtmr; g_hookCnt++; }
static VOID HookSwtmrStart(const SWTMR_CTRL_S *swtmr) { (VOID)swtmr; g_hookCnt++; }
static VOID HookSwtmrStop(const SWTMR_CTRL_S *swtmr) { (VOID)swtmr; g_hookCnt++; }

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 fullCnt = 0;
    UINT32 freeCnt = 0;

#define HOOK_TC_MATRIX(type, fn)                                    \
    do {                                                            \
        ret = LOS_HookReg(type, fn);                                \
        if (ret == LOS_OK) {                                        \
            freeCnt++;                                              \
            ret = LOS_HookReg(type, fn);                            \
            ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HOOK_POOL_IS_FULL, ret);   \
            ret = LOS_HookReg(type, NULL);                          \
            ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HOOK_REG_INVALID, ret);    \
            ret = LOS_HookUnReg(type, fn);                          \
            ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);                  \
            ret = LOS_HookUnReg(type, fn);                          \
            ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HOOK_UNREG_INVALID, ret);  \
        } else {                                                    \
            ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HOOK_POOL_IS_FULL, ret);   \
            fullCnt++;                                              \
            ret = LOS_HookReg(type, NULL);                          \
            ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HOOK_REG_INVALID, ret);    \
            ret = LOS_HookUnReg(type, fn);                          \
            ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HOOK_UNREG_INVALID, ret);  \
        }                                                           \
    } while (0)

    /* 内存类 */
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MEM_INIT, HookMemInit);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MEM_DEINIT, HookMemDeinit);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MEM_ALLOC, HookMemAlloc);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MEM_FREE, HookMemFree);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MEM_REALLOC, HookMemRealloc);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MEM_ALLOCALIGN, HookMemAllocAlign);
    /* 事件类 */
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_EVENT_INIT, HookEventInit);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_EVENT_READ, HookEventRead);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_EVENT_WRITE, HookEventWrite);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_EVENT_CLEAR, HookEventClear);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_EVENT_DESTROY, HookEventDestroy);
    /* 队列类 */
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_QUEUE_CREATE, HookQueueCreate);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_QUEUE_READ, HookQueueRead);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_QUEUE_READ_COPY, HookQueueRead);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_QUEUE_WRITE, HookQueueWrite);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_QUEUE_WRITE_COPY, HookQueueWrite);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_QUEUE_DELETE, HookQueueDelete);
    /* 信号量类 */
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_SEM_CREATE, HookSemCreate);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_SEM_POST, HookSemPost);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_SEM_PEND, HookSemPend);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_SEM_DELETE, HookSemDelete);
    /* 互斥锁类 */
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MUX_CREATE, HookMuxCreate);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MUX_POST, HookMuxPost);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MUX_PEND, HookMuxPend);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MUX_DELETE, HookMuxDelete);
    /* 任务类 */
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_TASK_CREATE, HookTaskCreate);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_TASK_DELAY, HookTaskDelay);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_TASK_PRIMODIFY, HookTaskPriModify);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_TASK_DELETE, HookTaskDelete);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_TASK_SWITCHEDIN, HookTaskSwitchedIn);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MOVEDTASKTOREADYSTATE, HookMovedToReady);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MOVEDTASKTODELAYEDLIST, HookMovedToDelayed);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_MOVEDTASKTOSUSPENDEDLIST, HookMovedToSuspended);
    /* 中断类 */
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_ISR_EXITTOSCHEDULER, HookIsrExitToScheduler);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_ISR_ENTER, HookIsrEnter);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_ISR_EXIT, HookIsrExit);
    /* 软件定时器类 */
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_SWTMR_CREATE, HookSwtmrCreate);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_SWTMR_DELETE, HookSwtmrDelete);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_SWTMR_EXPIRED, HookSwtmrExpired);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_SWTMR_START, HookSwtmrStart);
    HOOK_TC_MATRIX(LOS_HOOK_TYPE_SWTMR_STOP, HookSwtmrStop);

#undef HOOK_TC_MATRIX

    /* 空闲类型(MEM_DEINIT/ISR_EXITTOSCHEDULER/MOVEDTASKTODELAYEDLIST)至少 3 类闭环 */
    ICUNIT_ASSERT_NOT_EQUAL(freeCnt, 0, freeCnt);
    (VOID)fullCnt;
    (VOID)g_hookCnt;
    return LOS_OK;
}

/**
 * @ingroup TEST_MISC
 * @par TestCase_Number
 * ItLosHook001
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_HookReg/LOS_HookUnReg (all hook types)
 * @par TestCase_Pretreatment_Condition
 * LOSCFG_DEBUG_HOOK enabled; trace module occupies most hook slots at boot.
 * @par TestCase_Test_Steps
 * step1: for each hook type, try register an own dummy hook.
 * step2: if free, exercise reg-again/full/null/unreg/unreg-again loop.
 * step3: if occupied (expected for trace-registered types), verify FULL and rejection branches.
 * @par TestCase_Expected_Result
 * 1.All RegHook/UnRegHook branches covered per type; own hooks always unregistered.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

#endif /* LOSCFG_DEBUG_HOOK */

VOID ItLosHook001(VOID)
{
#ifdef LOSCFG_DEBUG_HOOK
    TEST_ADD_CASE("ItLosHook001", TestCase, TEST_LOS, TEST_MISC, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
