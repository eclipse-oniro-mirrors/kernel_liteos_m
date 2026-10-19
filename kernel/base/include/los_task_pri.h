/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LOS_TASK_PRI_H
#define _LOS_TASK_PRI_H

#include "los_task_base.h"
#include "los_sched_pri.h"
#include "los_task.h"
#include "los_arch_context.h"
#include "los_mp_pri.h"

#include "los_config.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef LOSCFG_TASK_STACK_STATIC_ALLOCATION
#if ((LOSCFG_BASE_CORE_TSK_IDLE_STACK_SIZE & (LOSCFG_STACK_POINT_ALIGN_SIZE - 1)) != 0)
#error "idle task stack not aligned"
#endif
#if ((LOSCFG_BASE_CORE_TSK_SWTMR_STACK_SIZE & (LOSCFG_STACK_POINT_ALIGN_SIZE - 1)) != 0)
#error "swtmr task stack not aligned"
#endif
#if ((LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE & (LOSCFG_STACK_POINT_ALIGN_SIZE - 1)) != 0)
#error "default task stack not aligned"
#endif
#endif

#define OS_TCB_FROM_PENDLIST(ptr)                       LOS_DL_LIST_ENTRY(ptr, LosTaskCB, pendList)
#define OS_TCB_FROM_TID(taskId)                         (((LosTaskCB *)g_osTaskCBArray) + (taskId))

/* Task signal types */
#define SIGNAL_NONE                 0U
#define SIGNAL_KILL                 (1U << 0)
#define SIGNAL_SUSPEND              (1U << 1)
#define SIGNAL_AFFI                 (1U << 2)
#ifdef LOSCFG_POSIX_API
#define SIGNAL_CANCEL               (1U << 3)
#define OS_TASK_CANCEL_VALUE        0xFFFFFFFF
#define SIGNAL_TERMINATE            (SIGNAL_CANCEL | SIGNAL_KILL)
#else
#define SIGNAL_TERMINATE            SIGNAL_KILL
#endif

/* default and non-running task's ownership id */
#define OS_TASK_INVALID_CPUID       0xFFFF

#ifdef LOSCFG_KERNEL_SMP
STATIC INLINE VOID OsTaskSignalSet(LosTaskCB *taskCB, UINT32 mpSignal)
{
    taskCB->mpSignal = mpSignal;
}
#else
STATIC INLINE VOID OsTaskSignalSet(LosTaskCB *taskCB, UINT32 signal)
{
    (VOID)taskCB;
    (VOID)signal;
}
#endif

extern VOID OsTaskProcSignal(VOID);
LITE_OS_SEC_TEXT_INIT UINT32 OsTaskDelete(UINT32 taskId, UINTPTR retVal);

STATIC INLINE BOOL OsTaskSuspendCheckOnRun(LosTaskCB *taskCB, UINT32 *ret)
{
    /* init default out return value */
    *ret = LOS_OK;

#ifdef LOSCFG_KERNEL_SMP
    /* ASYNCHRONIZED. No need to do task lock checking */
    if (taskCB->currCpu != ArchCurrCpuid()) {
        OsTaskSignalSet(taskCB, SIGNAL_SUSPEND);
        LOS_MpSchedule(CPUID_TO_AFFI_MASK(taskCB->currCpu));
        return FALSE;
    }
#else
    (VOID)taskCB;
#endif

    if (!OsPreemptableInSched()) {
        /* Suspending the current core's running task */
        *ret = LOS_ERRNO_TSK_SUSPEND_LOCKED;
        return FALSE;
    }

#ifdef LOSCFG_KERNEL_SMP
    if (OS_INT_ACTIVE) {
        /*
         * suspend running task in interrupt.
         * mask "suspend" signal and later suspend will be handled.
         */
        OsTaskSignalSet(taskCB, SIGNAL_SUSPEND);
        return FALSE;
    }
#endif

    return TRUE;
}

/*
 * Check if needs to do the suspend operation in the current task.
 * Return TRUE, if needs to do the suspension.
 * Return FALSE, if meets following circumstances:
 * 1. Do the suspension across cores, if SMP is enabled
 * 2. Do the suspension when preemption is disabled
 * 3. Do the suspension in hard-irq
 * then LOS_TaskSuspend will directly return with 'ret' value.
 */

STATIC INLINE BOOL OsTaskIsExit(const LosTaskCB *taskCB)
{
    return ((taskCB->taskStatus & (OS_TASK_STATUS_ZOMBIE | OS_TASK_STATUS_UNUSED)) != 0);
}

/**
 * @ingroup los_task
 * Time slice structure.
 */
typedef struct TaskTimeSlice {
    LosTaskCB               *task;                        /**< Current running task */
    UINT16                  time;                         /**< Expiration time point */
    UINT16                  tout;                         /**< Expiration duration */
} OsTaskRobin;
extern LosTaskCB            *g_osTaskCBArray;
extern UINT32               g_taskMaxNum;

/**
 * @ingroup los_task
 * @brief check task id's invalidation
 */
#define OS_TASK_ID_CHECK_INVALID(taskId)      (OS_TSK_GET_INDEX(taskId) >= g_taskMaxNum)

/**
 * @ingroup los_task
 * @brief the tasks blocked by delay, pend or suspend
 */
#define OS_CHECK_TASK_BLOCK         (OS_TASK_STATUS_DELAY |    \
                                     OS_TASK_STATUS_PEND |     \
                                     OS_TASK_STATUS_SUSPEND)

extern VOID                 OsIdleHandler(VOID);

STATIC INLINE LosTaskCB *OsCurrTaskGet(VOID)
{
    return (LosTaskCB *)ArchCurrTaskGet();
}

STATIC INLINE VOID OsCurrTaskSet(LosTaskCB *task)
{
    ArchCurrTaskSet((VOID *)task);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#ifdef LOSCFG_KERNEL_SMP
STATIC INLINE UINT32 OsSpinLockTaskRq(LosTaskCB *taskCB)
{
    (VOID)taskCB;
    UINT32 intSave;
    LOS_SpinLockSave(&g_taskSpin, &intSave);
    return intSave;
}

STATIC INLINE VOID OsSpinUnlockTaskRq(LosTaskCB *taskCB, UINT32 intSave)
{
    (VOID)taskCB;
    LOS_SpinUnlockRestore(&g_taskSpin, intSave);
}
#else
STATIC INLINE UINT32 OsSpinLockTaskRq(LosTaskCB *taskCB)
{
    (VOID)taskCB;
    return 0;
}

STATIC INLINE VOID OsSpinUnlockTaskRq(LosTaskCB *taskCB, UINT32 intSave)
{
    (VOID)taskCB;
    (VOID)intSave;
}
#endif

extern LOWPOWERIDLEHOOK OsLowpowerHookGet(VOID);
LITE_OS_SEC_TEXT VOID OsTaskScan(VOID);
LITE_OS_SEC_TEXT VOID OsTaskAdd2TimerList(LosTaskCB *taskCB, UINT32 timeout);
LITE_OS_SEC_TEXT VOID OsTimerListDelete(LosTaskCB *taskCB);

#endif /* _LOS_TASK_PRI_H */
