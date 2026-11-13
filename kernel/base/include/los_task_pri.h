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
#ifdef LOSCFG_BOX
/* BOX taskId encoding (refs los_task_pri.h:101-104): kernel ids stay plain,
 * box ids carry (boxId+1) in the high 16 bits; every TCB lookup routes to
 * the owning pool. One macro definition covers all call sites. */
#include "los_box_pri.h"    /* OS_BOX_GET_BID/GET_ID/SET_ID (no cycle: it only
                             * pulls the public los_task.h) */
#define OS_TASK_ID_IS_KER(taskId)                       ((((taskId) >> 16) & (UINT32)0xFFFF) == (UINT32)0)
extern LosTaskCB *g_boxTaskCBArray[LOSCFG_BASE_BOX_MAX_LIMIT];
extern BOOL OsBoxTaskIdCheckInvalid(UINT32 taskId);
/* generic create path, box-aware (refs OsTaskCreateOnly carries a boxPtr);
 * defined in los_task.c, called by the box create family in los_task_box.c */
extern UINT32 OsTaskCreateOnly(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack,
                               BOOL useUsrStack, const VOID *boxPtr);
/* ---- Gate/Exit pattern (refs IPC_SEC design, M5B-R refactor):
 * los_task.c calls generic gates; BOX logic stays in los_task_box.c.
 * Gates are STATIC INLINE when BOX=off for zero-cost baseline. ---- */
extern VOID OsTaskBoxCBInitDefault(LosTaskCB *taskCB);
extern UINT32 OsTaskCreateTCBGet(LosTaskCB **taskCB, const VOID *boxPtr);
extern VOID OsTaskCreateTCBRewind(LosTaskCB *taskCB, const VOID *boxPtr);
/* delete-path gate pair: ResRelease frees box resources while the TCB
 * fields are still live (before the topOfStack clear + security memset);
 * TCBRecycle links the wiped TCB back to its free list afterwards. */
extern VOID OsTaskResRelease(LosTaskCB *taskCB, UINT32 taskId);
extern VOID OsTaskTCBRecycle(LosTaskCB *taskCB, UINT32 taskId);
/* M5G static-create box core (los_task_box.c): the native
 * LOS_TaskCreateOnlyStatic entry (los_task.c) routes a box context here. */
extern UINT32 OsTaskCreateStaticDo(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack);
extern BOOL OsTaskBoxDelRunDefer(LosTaskCB *taskCB);
/* task pools owned by los_task.c - the hooks route TCBs into them */
extern LOS_DL_LIST g_losFreeTask;
extern LOS_DL_LIST g_taskRecycleList;
/* create-path predicate: a box create carries its own param checks */
#define OS_TASK_IS_BOX_CREATE(boxPtr)                   ((boxPtr) != NULL)
#define OS_TCB_FROM_TID(taskId) \
    ((OS_TASK_ID_IS_KER(taskId)) ? (&g_osTaskCBArray[OS_TSK_GET_INDEX(taskId)]) : \
        (g_boxTaskCBArray[OS_BOX_GET_BID(taskId)] + OS_BOX_GET_ID(taskId)))
#else
#define OS_TCB_FROM_TID(taskId)                         (((LosTaskCB *)g_osTaskCBArray) + (taskId))
/* BOX-off gates: STATIC INLINE baseline, compiler optimizes to zero overhead
 * (refs IPC_SEC los_mux.c L84-91 constraint: no print/assert, rely on inlining) */
#define OS_TASK_IS_BOX_CREATE(boxPtr)                   (0)
#define OsTaskBoxCBInitDefault(taskCB)                  ((void)0)
#define OsTaskBoxDelRunDefer(taskCB)                    (0)

STATIC INLINE UINT32 OsTaskCreateTCBGet(LosTaskCB **taskCB, const VOID *boxPtr)
{
    (VOID)boxPtr;
    if (LOS_ListEmpty(&g_losFreeTask)) {
        return LOS_ERRNO_TSK_TCB_UNAVAILABLE;
    }
    *taskCB = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(&g_losFreeTask));
    LOS_ListDelete(LOS_DL_LIST_FIRST(&g_losFreeTask));
    return LOS_OK;
}

STATIC INLINE VOID OsTaskCreateTCBRewind(LosTaskCB *taskCB, const VOID *boxPtr)
{
    (VOID)boxPtr;
    LOS_ListAdd(&g_losFreeTask, &taskCB->pendList);
}

/* BOX=off baseline: no box resources exist, so the release step is a
 * no-op; the recycle step is the free-list link (mirrors the kernel arm
 * of the BOX=on gate in los_task_box.c). */
STATIC INLINE VOID OsTaskResRelease(LosTaskCB *taskCB, UINT32 taskId)
{
    (VOID)taskCB;
    (VOID)taskId;
}

STATIC INLINE VOID OsTaskTCBRecycle(LosTaskCB *taskCB, UINT32 taskId)
{
    (VOID)taskId;
    LOS_ListAdd(&g_losFreeTask, &taskCB->pendList);
}
#endif

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

#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
extern LosTaskCB            g_mainTask[LOSCFG_KERNEL_CORE_NUM];
#endif

/**
 * @ingroup los_task
 * @brief check task id's invalidation
 */
#ifdef LOSCFG_BOX
/* BOX: encoding-aware validity (refs OsTaskIdCheck semantics): kernel ids
 * index g_osTaskCBArray, box ids decode against their per-box pool bounds. */
#define OS_TASK_ID_CHECK_INVALID(taskId)      OsBoxTaskIdCheckInvalid(taskId)
#else
#define OS_TASK_ID_CHECK_INVALID(taskId)      (OS_TSK_GET_INDEX(taskId) >= g_taskMaxNum)
#endif

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
