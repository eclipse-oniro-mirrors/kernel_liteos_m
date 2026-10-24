/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2022 Huawei Device Co., Ltd. All rights reserved.
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

#include "los_task_pri.h"
#include "los_arch_context.h"
#include "los_init.h"
#include "los_task_base.h"
#include "securec.h"
#include "los_config.h"
#include "los_debug.h"
#include "los_hook.h"
#include "los_interrupt.h"
#include "los_memory_pri.h"
#include "los_mpu.h"
#include "los_sched.h"
#include "los_sched_pri.h"
#include "los_priqueue_pri.h"
#include "los_mux.h"
#include "los_mux_pri.h"
#include "los_sem.h"
#include "los_sem_pri.h"
#include "los_timer.h"
#if (LOSCFG_BASE_CORE_CPUP == 1)
#include "los_cpup_pri.h"
#endif
#if (LOSCFG_KERNEL_PM == 1)
#include "los_pm.h"
#endif
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
#include "los_hpl_pri.h"
#endif

/**
 * @ingroup los_task
 * @brief the offset of task stack's top for skipping the magic word
 */
#define OS_TASK_STACK_TOP_OFFSET                4

#if (LOSCFG_EXC_HARDWARE_STACK_PROTECTION == 1)
/**
 * @ingroup los_task
 * @brief the size of task stack's protection area
 */
#define OS_TASK_STACK_PROTECT_SIZE              32
#endif

LITE_OS_SEC_BSS  LosTaskCB                           *g_osTaskCBArray = NULL;
LITE_OS_SEC_BSS  UINT32                              g_taskMaxNum;
LITE_OS_SEC_BSS  TSKSWITCHHOOK                       g_pfnUsrTskSwitchHook = NULL;
LITE_OS_SEC_BSS  IDLEHANDLERHOOK                     g_idleHandlerHook = NULL;

LITE_OS_SEC_DATA_INIT LOS_DL_LIST                    g_losFreeTask;
LITE_OS_SEC_DATA_INIT LOS_DL_LIST                    g_taskRecycleList;
LITE_OS_SEC_BSS UINT32 g_taskScheduled; /* one bit for each cores */

LITE_OS_SEC_BSS SPIN_LOCK_INIT(g_taskSpin);

#ifndef LOSCFG_TASK_STACK_DYNAMIC_ALLOCATION
#ifndef LOSCFG_TASK_STACK_STATIC_ALLOCATION
#error "no choice for task stack alloc mode"
#endif
#endif

#ifdef LOSCFG_KERNEL_PM
STATIC LOWPOWERIDLEHOOK g_lowPowerHook = NULL;
#endif

#if (LOSCFG_BASE_CORE_EXC_TSK_SWITCH == 1)
TaskSwitchInfo g_taskSwitchInfo;
#endif

LITE_OS_SEC_TEXT_INIT UINT32 OsTaskDelete(UINT32 taskId, UINTPTR retVal);

LITE_OS_SEC_TEXT VOID OsTaskAdd2TimerList(LosTaskCB *taskCB, UINT32 timeout);

STATIC VOID OsTaskStackFree(LosTaskCB *taskCB)
{
#ifdef LOSCFG_TASK_STACK_DYNAMIC_ALLOCATION
    if (taskCB->usrStack == 0) {
        UINTPTR stackPtr;
#if (LOSCFG_EXC_HARDWARE_STACK_PROTECTION == 1)
        stackPtr = taskCB->topOfStack - OS_TASK_STACK_PROTECT_SIZE;
#else
        stackPtr = taskCB->topOfStack;
#endif
        LOS_SpinUnlockNoSched(&g_taskSpin);
        (VOID)LOS_MemFree(OS_TASK_STACK_ADDR, (VOID *)stackPtr);
        LOS_SpinLock(&g_taskSpin);
    }
#endif
    taskCB->topOfStack = 0;
    UINT32 taskId = taskCB->taskId;
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
    VOID *hplHandle = taskCB->hpl;
#endif
    (VOID)memset_s(taskCB, sizeof(LosTaskCB), 0, sizeof(LosTaskCB));
    taskCB->taskId = taskId;
    taskCB->taskStatus = OS_TASK_STATUS_UNUSED;
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
    taskCB->hpl = hplHandle;
    if (taskCB->hpl != NULL) {
        OsHplPreDelete(taskCB->hpl);
    }
#endif
    LOS_ListAdd(&g_losFreeTask, &taskCB->pendList);
}

#ifdef LOSCFG_TASK_JOINABLE
STATIC VOID OsTaskJoinPost(LosTaskCB *taskCB)
{
    LOS_ASSERT(LOS_SpinHeld(&g_taskSpin));

    if (OS_TASK_IS_ALREADY_JOIN(taskCB)) {
        taskCB->joinner->taskStatus &= ~OS_TASK_STATUS_PEND;
        taskCB->joinner->taskStatus |= OS_TASK_STATUS_DELAY;
        OsTaskAdd2TimerList(taskCB->joinner, 1); /* Wake up by 1 tick */
    }
}

STATIC VOID OsTaskCancelJoin(LosTaskCB *taskCB)
{
    if (OS_TASK_IS_JOINING(taskCB)) {
        taskCB->joined->joinner = NULL;
        taskCB->joined = NULL;
    }
}
#endif /* LOSCFG_TASK_JOINABLE */

VOID LOS_TaskResRecycle(VOID)
{
    UINT32 intSave;
    LosTaskCB *taskCB = NULL;

    SCHEDULER_LOCK(intSave);

    while (!LOS_ListEmpty(&g_taskRecycleList)) {
        taskCB = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(&g_taskRecycleList));
        LOS_ListDelete(LOS_DL_LIST_FIRST(&g_taskRecycleList));
        OsTaskStackFree(taskCB);
    }

    SCHEDULER_UNLOCK(intSave);
}

/*****************************************************************************
 Function    : OsIdleTask
 Description : Idle task.
 Input       : None
 Output      : None
 Return      : None
 *****************************************************************************/
LITE_OS_SEC_TEXT WEAK VOID OsIdleTask(VOID)
{
    while (1) {
        LOS_TaskResRecycle();
        OsIdleHandler();

#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
        OsHplIdleCompressLog();
#endif
#ifdef LOSCFG_KERNEL_PM
        LOWPOWERIDLEHOOK lowPowerHook = OsLowpowerHookGet();
        if (lowPowerHook != NULL) {
            lowPowerHook();
            continue;                       /* the hook performs its own sleep */
        }
#endif
        (VOID)ArchEnterSleep();
    }
}

/*
 * Description : Add task to sorted delay list.
 * Input       : taskCB  --- task control block
 *               timeout --- wait time, ticks
 */
LITE_OS_SEC_TEXT VOID OsTaskAdd2TimerList(LosTaskCB *taskCB, UINT32 timeout)
{
    OsSchedAddSortLink(&OsPercpuGet()->taskSortLink, &taskCB->sortList, timeout);
#ifdef LOSCFG_KERNEL_SMP
    taskCB->timerCpu = ArchCurrCpuid();
#endif
}

LITE_OS_SEC_TEXT VOID OsTimerListDelete(LosTaskCB *taskCB)
{
    SortLinkAttribute *sortLinkHeader = NULL;

#ifdef LOSCFG_KERNEL_SMP
    /*
     * the task delay timer is on the specific processor,
     * we need delete the timer from that processor's sortlink.
     */
    sortLinkHeader = &g_percpu[taskCB->timerCpu].taskSortLink;
#else
    sortLinkHeader = &g_percpu[0].taskSortLink;
#endif
    OsSchedDeleteSortLink(sortLinkHeader, &taskCB->sortList);
}

LITE_OS_SEC_TEXT VOID OsTaskScan(VOID)
{
    SortLinkList *sortList = NULL;
    LosTaskCB *taskCB = NULL;
    BOOL needSchedule = FALSE;
    UINT16 tempStatus;
    LOS_DL_LIST *listObject = NULL;
    SortLinkAttribute *taskSortLink = NULL;

    taskSortLink = &OsPercpuGet()->taskSortLink;
    OsTickScanAdvance(taskSortLink);
    SORTLINK_LISTOBJ_GET(listObject, taskSortLink);

    /*
     * When task is pended with timeout, the task block is on the timeout sortlink
     * (per cpu) and ipc(mutex,sem and etc.)'s block at the same time, it can be waken
     * up by either timeout or corresponding ipc it's waiting.
     *
     * Now synchronize sortlink procedure is used, therefore the whole task scan needs
     * to be protected, preventing another core from doing sortlink deletion at same time.
     */
    LOS_SpinLock(&g_taskSpin);

    if (LOS_ListEmpty(listObject)) {
        LOS_SpinUnlock(&g_taskSpin);
        return;
    }
    sortList = LOS_DL_LIST_ENTRY(listObject->pstNext, SortLinkList, sortLinkNode);
    while (ROLLNUM(sortList->idxRollNum) == 0) {
        LOS_ListDelete(&sortList->sortLinkNode);
        taskCB = LOS_DL_LIST_ENTRY(sortList, LosTaskCB, sortList);
        taskCB->taskStatus &= ~OS_TASK_STATUS_PEND_TIME;
        tempStatus = taskCB->taskStatus;
        if ((tempStatus & (OS_TASK_STATUS_PEND | OS_TASK_STATUS_DELAY)) == 0) {
            continue;
        }
        if (tempStatus & OS_TASK_STATUS_PEND) {
            taskCB->taskStatus &= ~OS_TASK_STATUS_PEND;
            taskCB->taskStatus |= OS_TASK_STATUS_TIMEOUT;
            LOS_ListDelete(&taskCB->pendList);
            taskCB->taskSem = NULL;
            taskCB->taskMux = NULL;
        } else {
            taskCB->taskStatus &= ~OS_TASK_STATUS_DELAY;
        }

        if (!(tempStatus & OS_TASK_STATUS_SUSPEND)) {
            OsSchedTaskEnQueue(taskCB);
            needSchedule = TRUE;
        }

        if (LOS_ListEmpty(listObject)) {
            break;
        }

        sortList = LOS_DL_LIST_ENTRY(listObject->pstNext, SortLinkList, sortLinkNode);
    }

    LOS_SpinUnlock(&g_taskSpin);

    if (needSchedule == TRUE) {
        LOS_MpSchedule(OS_MP_CPU_ALL);
        LOS_Schedule();
    }
}

/*****************************************************************************
 Function    : OsConvertTskStatus
 Description : Convert task status to string.
 Input       : taskStatus    --- task status
 Output      : None
 Return      : string
 *****************************************************************************/
LITE_OS_SEC_TEXT_MINOR UINT8 *OsConvertTskStatus(UINT16 taskStatus)
{
    if (taskStatus & OS_TASK_STATUS_RUNNING) {
        return (UINT8 *)"Running";
    } else if (taskStatus & OS_TASK_STATUS_READY) {
        return (UINT8 *)"Ready";
    } else if (taskStatus & OS_TASK_STATUS_ZOMBIE) {
        return (UINT8 *)"Exit";
    } else if (taskStatus & OS_TASK_STATUS_SUSPEND) {
        return (UINT8 *)"Suspend";
    } else if (taskStatus & OS_TASK_STATUS_DELAY) {
        return (UINT8 *)"Delay";
    } else if (taskStatus & OS_TASK_STATUS_PEND) {
        if (taskStatus & OS_TASK_STATUS_PEND_TIME) {
            return (UINT8 *)"PendTime";
        }
        return (UINT8 *)"Pend";
    }

    return (UINT8 *)"Impossible";
}

UINT32 OsGetTaskWaterLine(UINT32 taskId)
{
    UINT32 *stackPtr = NULL;
    UINT32 peakUsed;

    if (*(UINT32 *)(UINTPTR)OS_TCB_FROM_TID(taskId)->topOfStack == OS_TASK_MAGIC_WORD) {
        stackPtr = (UINT32 *)(UINTPTR)(OS_TCB_FROM_TID(taskId)->topOfStack + OS_TASK_STACK_TOP_OFFSET);
        while ((stackPtr < (UINT32 *)(OS_TCB_FROM_TID(taskId)->stackPointer)) && (*stackPtr == OS_TASK_STACK_INIT)) {
            stackPtr += 1;
        }
        peakUsed = OS_TCB_FROM_TID(taskId)->stackSize -
            ((UINT32)(UINTPTR)stackPtr - OS_TCB_FROM_TID(taskId)->topOfStack);
    } else {
        PRINT_ERR("CURRENT task %s stack overflow!\n", OS_TCB_FROM_TID(taskId)->taskName);
        peakUsed = OS_NULL_INT;
    }
    return peakUsed;
}

#if (LOSCFG_BASE_CORE_CPUP == 1)
STATIC UINT32 GetAllTskCpupInfo(CPUP_INFO_S **cpuLessOneSec,
                                CPUP_INFO_S **cpuTenSec,
                                CPUP_INFO_S **cpuOneSec)
{
    if ((cpuLessOneSec == NULL) || (cpuTenSec == NULL) || (cpuOneSec == NULL)) {
        return OS_ERROR;
    }
    *cpuLessOneSec = (CPUP_INFO_S *)LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, sizeof(CPUP_INFO_S) * g_taskMaxNum);
    if (*cpuLessOneSec == NULL) {
        PRINT_ERR("%s[%d] malloc failure!\n", __FUNCTION__, __LINE__);
        return OS_ERROR;
    }
    // Ignore the return code when matching CSEC rule 6.6(3).
    (VOID)memset_s((VOID *)(*cpuLessOneSec), sizeof(CPUP_INFO_S) * g_taskMaxNum,
                   (INT32)0, sizeof(CPUP_INFO_S) * g_taskMaxNum);

    *cpuTenSec = (CPUP_INFO_S *)LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, sizeof(CPUP_INFO_S) * g_taskMaxNum);
    if (*cpuTenSec == NULL) {
        PRINT_ERR("%s[%d] malloc failure!\n", __FUNCTION__, __LINE__);
        (VOID)LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, *cpuLessOneSec);
        *cpuLessOneSec = NULL;
        return OS_ERROR;
    }
    // Ignore the return code when matching CSEC rule 6.6(3).
    (VOID)memset_s((VOID *)(*cpuTenSec), sizeof(CPUP_INFO_S) * g_taskMaxNum,
                   (INT32)0, sizeof(CPUP_INFO_S) * g_taskMaxNum);

    *cpuOneSec = (CPUP_INFO_S *)LOS_MemAlloc((VOID *)OS_SYS_MEM_ADDR, sizeof(CPUP_INFO_S) * g_taskMaxNum);
    if (*cpuOneSec == NULL) {
        PRINT_ERR("%s[%d] malloc failure!\n", __FUNCTION__, __LINE__);
        (VOID)LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, *cpuLessOneSec);
        (VOID)LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, *cpuTenSec);
        return OS_ERROR;
    }
    // Ignore the return code when matching CSEC rule 6.6(3).
    (VOID)memset_s((VOID *)(*cpuOneSec), sizeof(CPUP_INFO_S) * g_taskMaxNum,
                   (INT32)0, sizeof(CPUP_INFO_S) * g_taskMaxNum);

    LOS_TaskLock();
    (VOID)LOS_AllTaskCpuUsage(*cpuLessOneSec, CPUP_ALL_TIME);
    (VOID)LOS_AllTaskCpuUsage(*cpuTenSec, CPUP_IN_10S);
    (VOID)LOS_AllTaskCpuUsage(*cpuOneSec, CPUP_IN_1S);
    LOS_TaskUnlock();

    return LOS_OK;
}
#endif

STATIC VOID PrintTskInfo(const LosTaskCB *taskCB)
{
    UINT32 semID;

    if (taskCB->taskStatus & OS_TASK_STATUS_ZOMBIE) {
        PRINTK("%4u%9u%10s%#10x%#10x%#11x%#11x%#10x%#7x",
               taskCB->taskId, taskCB->priority, OsConvertTskStatus(taskCB->taskStatus),
               taskCB->stackSize, 0, 0, 0, 0, 0);
        return;
    }

    semID = (taskCB->taskSem == NULL) ? OS_NULL_SHORT : (((LosSemCB *)taskCB->taskSem)->semId);
    PRINTK("%4u%9u%10s%#10x%#10x%#11x%#11x%#10x%#7x",
           taskCB->taskId, taskCB->priority, OsConvertTskStatus(taskCB->taskStatus),
           taskCB->stackSize, OsGetTaskWaterLine(taskCB->taskId),
           (UINT32)(UINTPTR)taskCB->stackPointer, taskCB->topOfStack, taskCB->eventMask, semID);
    return;
}

STATIC VOID PrintTskInfoHeader(VOID)
{
    PRINTK("\r\n TID  Priority   Status StackSize WaterLine StackPoint TopOfStack EventMask  SemID");
#if (LOSCFG_TASK_MEM_USED == 1)
    PRINTK(" AllocSize");
#endif
#if (LOSCFG_BASE_CORE_CPUP == 1)
    PRINTK("  CPUUSE CPUUSE10s CPUUSE1s ");
#endif /* LOSCFG_BASE_CORE_CPUP */
    PRINTK("  TaskEntry name\n");
    PRINTK(" ---  -------- -------- ");
    PRINTK("--------- --------- ---------- ---------- --------- ------ ");
#if (LOSCFG_TASK_MEM_USED == 1)
    PRINTK("--------- ");
#endif
#if (LOSCFG_BASE_CORE_CPUP == 1)
    PRINTK("------- --------- --------  ");
#endif /* LOSCFG_BASE_CORE_CPUP */
    PRINTK("---------- ----\n");
}

#if (LOSCFG_TASK_MEM_USED == 1)
STATIC UINT32                              g_taskMemUsed[LOSCFG_BASE_CORE_TSK_LIMIT + 1];
#endif

/*****************************************************************************
 Function    : OsGetAllTskInfo
 Description : Get all task info.
 Input       : None
 Output      : None
 Return      : None
 *****************************************************************************/
LITE_OS_SEC_TEXT_MINOR UINT32 OsGetAllTskInfo(VOID)
{
#if (LOSCFG_KERNEL_PRINTF != 0)
    LosTaskCB    *taskCB = (LosTaskCB *)NULL;
    UINT32       loopNum;
#if (LOSCFG_BASE_CORE_CPUP == 1)
    CPUP_INFO_S *cpuLessOneSec = (CPUP_INFO_S *)NULL;
    CPUP_INFO_S *cpuTenSec = (CPUP_INFO_S *)NULL;
    CPUP_INFO_S *cpuOneSec = (CPUP_INFO_S *)NULL;
#endif

#if (LOSCFG_TASK_MEM_USED == 1)
    (VOID)memset_s(g_taskMemUsed, sizeof(UINT32) * g_taskMaxNum, 0, sizeof(UINT32) * g_taskMaxNum);
    OsTaskMemUsed((VOID *)OS_SYS_MEM_ADDR, g_taskMemUsed, g_taskMaxNum);
#endif

#if (LOSCFG_BASE_CORE_CPUP == 1)
    if (GetAllTskCpupInfo(&cpuLessOneSec, &cpuTenSec, &cpuOneSec) != LOS_OK) {
        return OS_ERROR;
    }
#endif /* LOSCFG_BASE_CORE_CPUP */

    PrintTskInfoHeader();

    for (loopNum = 0; loopNum < g_taskMaxNum; loopNum++) {
        taskCB = (((LosTaskCB *)g_osTaskCBArray) + loopNum);
        if (taskCB->taskStatus & OS_TASK_STATUS_UNUSED) {
            continue;
        }

        PrintTskInfo(taskCB);
#if (LOSCFG_TASK_MEM_USED == 1)
        PRINTK("%#10x", g_taskMemUsed[loopNum]);
#endif

#if (LOSCFG_BASE_CORE_CPUP == 1)
        PRINTK("%6u.%-2u%7u.%-2u%6u.%-2u ",
               cpuLessOneSec[taskCB->taskId].uwUsage / LOS_CPUP_PRECISION_MULT,
               cpuLessOneSec[taskCB->taskId].uwUsage % LOS_CPUP_PRECISION_MULT,
               cpuTenSec[taskCB->taskId].uwUsage / LOS_CPUP_PRECISION_MULT,
               cpuTenSec[taskCB->taskId].uwUsage % LOS_CPUP_PRECISION_MULT,
               cpuOneSec[taskCB->taskId].uwUsage / LOS_CPUP_PRECISION_MULT,
               cpuOneSec[taskCB->taskId].uwUsage % LOS_CPUP_PRECISION_MULT);
#endif /* LOSCFG_BASE_CORE_CPUP */
        PRINTK("%#10x %-32s\n", (UINT32)(UINTPTR)taskCB->taskEntry, taskCB->taskName);
    }

#if (LOSCFG_BASE_CORE_CPUP == 1)
    (VOID)LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, cpuLessOneSec);
    (VOID)LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, cpuTenSec);
    (VOID)LOS_MemFree((VOID *)OS_SYS_MEM_ADDR, cpuOneSec);
#endif
#endif
    return LOS_OK;
}

/*****************************************************************************
 Function    : OsTaskInit
 Description : Task init function.
 Input       : None
 Output      : None
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
LITE_OS_SEC_BSS STATIC LOS_DL_LIST g_taskSortlink[LOSCFG_KERNEL_CORE_NUM][OS_TSK_SORTLINK_LEN];

LITE_OS_SEC_TEXT_INIT_LEVEL_ONE UINT32 OsTaskInit(VOID)
{
    UINT32 size;
    UINT32 index;

    size = (KERNEL_TSK_LIMIT + 1) * sizeof(LosTaskCB);
    g_osTaskCBArray = (LosTaskCB *)LOS_MemAlloc(m_aucSysMem0, size);
    if (g_osTaskCBArray == NULL) {
        return LOS_ERRNO_TSK_NO_MEMORY;
    }

    // Ignore the return code when matching CSEC rule 6.6(1).
    (VOID)memset_s(g_osTaskCBArray, size, 0, size);
    LOS_ListInit(&g_losFreeTask);
    LOS_ListInit(&g_taskRecycleList);
    for (index = 0; index < KERNEL_TSK_LIMIT; index++) {
        g_osTaskCBArray[index].taskStatus = OS_TASK_STATUS_UNUSED;
        g_osTaskCBArray[index].taskId = index;
        LOS_ListTailInsert(&g_losFreeTask, &g_osTaskCBArray[index].pendList);
    }

    OsPercpuGet()->idleTaskId = OS_INVALID;

    OsPriQueueInit();

    /* init sortlink for each core */
    for (index = 0; index < LOSCFG_KERNEL_CORE_NUM; index++) {
        OsSortLinkInit(&g_percpu[index].taskSortLink, g_taskSortlink[index]);
    }
    g_taskMaxNum = KERNEL_TSK_LIMIT;
    SchedTimeBaseInit();

    return LOS_OK;
}

LOS_SYS_INIT(OsTaskInit, SYS_INIT_LEVEL_KERNEL, SYS_INIT_SYNC_0);

/*****************************************************************************
 Function    : OsGetIdleTaskId
 Description : Get the idle task ID of the current CPU.
 Input       : None
 Output      : None
 Return      : idle task ID
 *****************************************************************************/
LITE_OS_SEC_TEXT UINT32 OsGetIdleTaskId(VOID)
{
    Percpu *perCpu = OsPercpuGet();
    return perCpu->idleTaskId;
}

#ifdef LOSCFG_TASK_STACK_STATIC_ALLOCATION
STATIC LITE_OS_SEC_BSS UINT8 g_osIdleTaskStack[LOSCFG_KERNEL_CORE_NUM][LOSCFG_BASE_CORE_TSK_IDLE_STACK_SIZE]
    LITE_OS_ATTR_ALIGN(LOSCFG_STACK_POINT_ALIGN_SIZE);
#endif

/*****************************************************************************
 Function    : OsIdleTaskCreate
 Description : Create idle task.
 Input       : None
 Output      : None
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
LITE_OS_SEC_TEXT_INIT_LEVEL_ONE UINT32 OsIdleTaskCreate(VOID)
{
    UINT32 ret;
    TSK_INIT_PARAM_S taskInitParam = {0};
    Percpu *perCpu = OsPercpuGet();
    UINT32 *idleTaskId = &perCpu->idleTaskId;

    taskInitParam.pfnTaskEntry = (TSK_ENTRY_FUNC)OsIdleTask;
    taskInitParam.uwStackSize = KERNEL_TSK_IDLE_STACK_SIZE;
    taskInitParam.pcName = "IdleCore000";
    taskInitParam.usTaskPrio = LOS_TASK_PRIORITY_LOWEST;
#ifdef LOSCFG_KERNEL_SMP
    taskInitParam.usCpuAffiMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());
#endif

#ifdef LOSCFG_TASK_STACK_STATIC_ALLOCATION
    ret = LOS_TaskCreateStatic(idleTaskId, &taskInitParam, g_osIdleTaskStack[ArchCurrCpuid()]);
#else
    ret = LOS_TaskCreate(idleTaskId, &taskInitParam);
#endif
    if (ret == LOS_OK) {
        OS_TCB_FROM_TID(*idleTaskId)->taskFlags |= OS_TASK_FLAG_SYSTEM;
    }

    return ret;
}

LOS_SYS_INIT(OsIdleTaskCreate, SYS_INIT_LEVEL_KERNEL, SYS_INIT_SYNC_3);

/*
 * Description : get id of current running task.
 * Return      : task id
 */
/*****************************************************************************
 Function    : LOS_CurTaskIDGet
 Description : get id of current running task.
 Input       : None
 Output      : None
 Return      : task id
 *****************************************************************************/
LITE_OS_SEC_TEXT UINT32 LOS_CurTaskIDGet(VOID)
{
    LosTaskCB *runTask = OsCurrTaskGet();

    if (runTask == NULL) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }
    return runTask->taskId;
}

/*****************************************************************************
 Function    : LOS_NextTaskIDGet
 Description : get id of next running task.
 Input       : None
 Output      : None
 Return      : task id
 *****************************************************************************/
LITE_OS_SEC_TEXT UINT32 LOS_NextTaskIDGet(VOID)
{
    UINT32 intSave;
    SCHEDULER_LOCK(intSave);
    UINT32 taskId = OsGetTopTask()->taskId;
    SCHEDULER_UNLOCK(intSave);

    return taskId;
}

/*****************************************************************************
 Function    : LOS_CurTaskNameGet
 Description : get name of current running task.
 Input       : None
 Output      : None
 Return      : task name
 *****************************************************************************/
LITE_OS_SEC_TEXT CHAR *LOS_CurTaskNameGet(VOID)
{
    LosTaskCB *runTask = OsCurrTaskGet();

    if (runTask != NULL) {
        return runTask->taskName;
    }

    return NULL;
}

#if (LOSCFG_BASE_CORE_TSK_MONITOR == 1)
#if (LOSCFG_EXC_HARDWARE_STACK_PROTECTION == 0)
/*****************************************************************************
 Function    : OsHandleRunTaskStackOverflow
 Description : handle stack overflow exception of the run task.
 Input       : None
 Output      : None
 Return      : None
 *****************************************************************************/
LITE_OS_SEC_TEXT STATIC VOID OsHandleRunTaskStackOverflow(const LosTaskCB *oldTask)
{
    PRINT_ERR("CURRENT task ID: %s:%d stack overflow!\n",
              oldTask->taskName, oldTask->taskId);
    OsDoExcHook(EXC_STACKOVERFLOW);
}

/*****************************************************************************
 Function    : OsHandleNewTaskStackOverflow
 Description : handle stack overflow exception of the new task.
 Input       : None
 Output      : None
 Return      : None
 *****************************************************************************/
LITE_OS_SEC_TEXT STATIC VOID OsHandleNewTaskStackOverflow(const LosTaskCB *newTask)
{
    LosTaskCB *tmp = NULL;

    PRINT_ERR("HIGHEST task ID: %s:%d SP error!\n",
              newTask->taskName, newTask->taskId);
    PRINT_ERR("HIGHEST task StackPointer: 0x%x TopOfStack: 0x%x\n",
              (UINT32)(UINTPTR)(newTask->stackPointer), newTask->topOfStack);

    tmp = OsCurrTaskGet();
    OsCurrTaskSet((LosTaskCB *)newTask);
    OsDoExcHook(EXC_STACKOVERFLOW);
    OsCurrTaskSet(tmp);
}
#else
LITE_OS_SEC_TEXT STATIC VOID OsTaskStackProtect(const LosTaskCB *newTask)
{
    MPU_CFG_PARA mpuAttr = {0};
    STATIC INT32 id = -1;

    if (id == -1) {
        id = ArchMpuUnusedRegionGet();
        if (id < 0) {
            PRINT_ERR("%s %d, get unused id failed!\n", __FUNCTION__, __LINE__);
            return;
        }
    }

    mpuAttr.baseAddr = newTask->topOfStack - OS_TASK_STACK_PROTECT_SIZE;
    mpuAttr.size = OS_TASK_STACK_PROTECT_SIZE;
    mpuAttr.memType = MPU_MEM_ON_CHIP_RAM;
    mpuAttr.executable = MPU_NON_EXECUTABLE;
    mpuAttr.shareability = MPU_NO_SHARE;
    mpuAttr.permission = MPU_RO_BY_PRIVILEGED_ONLY;

    ArchMpuDisable();
    (VOID)ArchMpuDisableRegion(id);
    (VOID)ArchMpuSetRegion(id, &mpuAttr);
    ArchMpuEnable(1);
}
#endif
#endif

#if (LOSCFG_BASE_CORE_TSK_MONITOR == 1)
LITE_OS_SEC_TEXT_MINOR UINT32 OsTaskMonInit(VOID)
{
    g_pfnUsrTskSwitchHook = NULL;
#if (LOSCFG_BASE_CORE_EXC_TSK_SWITCH == 1)
    // Ignore the return code when matching CSEC rule 6.6(4).
    (VOID)memset_s(&g_taskSwitchInfo, sizeof(TaskSwitchInfo), 0, sizeof(TaskSwitchInfo));
    g_taskSwitchInfo.cntInfo.maxCnt = OS_TASK_SWITCH_INFO_COUNT;
#endif
    return LOS_OK;
}

LOS_SYS_INIT(OsTaskMonInit, SYS_INIT_LEVEL_KERNEL, SYS_INIT_SYNC_0);
#endif /* LOSCFG_BASE_CORE_TSK_MONITOR */

/*****************************************************************************
 Function    : LOS_TaskSwitchHookReg
 Description : Register a user-defined task switching hook. The hook will be
               called from OsSchedTaskSwitch on every task context switch,
               after task status fields are updated and before the actual
               register-level context switch. Runs in PendSV interrupt context
               (interrupts disabled, MSP stack) - hook must be interrupt-safe.
 Input       : hook - the hook function to register, NULL to deregister
 Output      : None
 Return      : None
 *****************************************************************************/
LITE_OS_SEC_TEXT_MINOR VOID LOS_TaskSwitchHookReg(TSKSWITCHHOOK hook)
{
    g_pfnUsrTskSwitchHook = hook;
}

#if (LOSCFG_BASE_CORE_TSK_MONITOR == 1)
/*****************************************************************************
 Function    : OsTaskSwitchCheck
 Description : Check task switch
 Input       : Node
 Output      : None
 Return      : None
 *****************************************************************************/
LITE_OS_SEC_TEXT_MINOR VOID OsTaskSwitchCheck(const LosTaskCB *oldTask, const LosTaskCB *newTask)
{
#if (LOSCFG_EXC_HARDWARE_STACK_PROTECTION == 0)
    UINT32 endOfStack = newTask->topOfStack + newTask->stackSize;

    if ((*(UINT32 *)(UINTPTR)(oldTask->topOfStack)) != OS_TASK_MAGIC_WORD) {
        OsHandleRunTaskStackOverflow(oldTask);
    }
    if (((UINT32)(UINTPTR)(newTask->stackPointer) <= (newTask->topOfStack)) ||
        ((UINT32)(UINTPTR)(newTask->stackPointer) > endOfStack)) {
        OsHandleNewTaskStackOverflow(newTask);
    }
#else
    OsTaskStackProtect(newTask);
#endif

#if (LOSCFG_BASE_CORE_EXC_TSK_SWITCH == 1)
    g_taskSwitchInfo.pid[g_taskSwitchInfo.idx] = (UINT16)(newTask->taskId);

    errno_t ret = memcpy_s(g_taskSwitchInfo.name[g_taskSwitchInfo.idx], LOS_TASK_NAMELEN,
                           newTask->taskName, LOS_TASK_NAMELEN);
    if (ret != EOK) {
        PRINT_ERR("exc task switch copy file name failed!\n");
    }
    g_taskSwitchInfo.name[g_taskSwitchInfo.idx][LOS_TASK_NAMELEN - 1] = '\0';

    if (++g_taskSwitchInfo.idx == OS_TASK_SWITCH_INFO_COUNT) {
        g_taskSwitchInfo.idx = 0;
        g_taskSwitchInfo.cntInfo.isFull = TRUE;
    }
#endif

    LOSCFG_BASE_CORE_TSK_SWITCH_HOOK();

#if (LOSCFG_BASE_CORE_CPUP == 1)
    OsTaskCycleEndStart(newTask);
#endif
}
#endif

#ifdef LOSCFG_KERNEL_PM
LITE_OS_SEC_TEXT_MINOR VOID LOS_LowpowerHookReg(LOWPOWERIDLEHOOK hook)
{
    g_lowPowerHook = hook;
}

LOWPOWERIDLEHOOK OsLowpowerHookGet(VOID)
{
    return g_lowPowerHook;
}
#endif

LITE_OS_SEC_TEXT_MINOR VOID LOS_IdleHandlerHookReg(IDLEHANDLERHOOK hook)
{
    g_idleHandlerHook = hook;
}

VOID OsIdleHandler(VOID)
{
    IDLEHANDLERHOOK idleHandlerHook = g_idleHandlerHook;
    if (idleHandlerHook != NULL) {
        idleHandlerHook();
    }
}

/*
 * Description : All task entry
 * Input       : taskId     --- The ID of the task to be run
 */
/*****************************************************************************
 Function    : OsTaskEntry
 Description : All task entry
 Input       : taskId     --- The ID of the task to be run
 Output      : None
 Return      : None
 *****************************************************************************/
LITE_OS_SEC_TEXT_INIT VOID OsTaskEntry(UINT32 taskId)
{
    LosTaskCB *taskCB = OS_TCB_FROM_TID(taskId);
    VOID *ret = NULL;

    LOS_SpinUnlockNoSched(&g_taskSpin);
    (VOID)LOS_IntUnLock();

#ifndef LOSCFG_TASK_JOINABLE
#ifdef  LOSCFG_KERNEL_TASK_ENTRY_VOID_PTR
    (VOID)taskCB->taskEntry(taskCB->args);
#else
    (VOID)taskCB->taskEntry(taskCB->arg);
#endif
#else
#ifdef  LOSCFG_KERNEL_TASK_ENTRY_VOID_PTR
    taskCB->taskRetval = taskCB->taskEntry(taskCB->args);
#else
    taskCB->taskRetval = taskCB->taskEntry(taskCB->arg);
#endif
    ret = taskCB->taskRetval;
#endif
    UINT32 intSave = LOS_IntLock();
    OsPercpuGet()->taskLockCnt = 0;
    LOS_IntRestore(intSave);
    (VOID)OsTaskDelete(taskCB->taskId, (UINTPTR)ret);
}

STATIC UINT32 OsTaskInitParamCheck(const TSK_INIT_PARAM_S *initParam)
{
    if (initParam == NULL) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }

    if (initParam->pcName == NULL) {
        return LOS_ERRNO_TSK_NAME_EMPTY;
    }

    if (initParam->pfnTaskEntry == NULL) {
        return LOS_ERRNO_TSK_ENTRY_NULL;
    }

    if (initParam->usTaskPrio > LOS_TASK_PRIORITY_LOWEST) {
        return LOS_ERRNO_TSK_PRIOR_ERROR;
    }

#ifdef LOSCFG_KERNEL_SMP
    if ((initParam->usCpuAffiMask != 0) &&
        (!(initParam->usCpuAffiMask & LOSCFG_KERNEL_CPU_MASK))) {
        return LOS_ERRNO_TSK_CPU_AFFINITY_MASK_ERR;
    }
#endif

    if ((initParam->usTaskPrio == LOS_TASK_PRIORITY_LOWEST)
        && (initParam->pfnTaskEntry != OS_IDLE_TASK_ENTRY)) {
        return LOS_ERRNO_TSK_PRIOR_ERROR;
    }

    return LOS_OK;
}

#ifdef LOSCFG_TASK_STACK_STATIC_ALLOCATION
/*****************************************************************************
 Function    : LOS_TaskCreateOnly
 Description : Create a task and suspend
 Input       : taskInitParam --- Task init parameters
 Output      : taskId        --- Save task ID
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
STATIC UINT32 OsTaskCreateParamCheckStatic(const UINT32 *taskId,
    const TSK_INIT_PARAM_S *initParam, const VOID *topStack)
{
    UINT32 ret;
    UINT32 minStackSize;

    if (taskId == NULL) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    if (topStack == NULL) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }

    ret = OsTaskInitParamCheck(initParam);
    if (ret != LOS_OK) {
        return ret;
    }

    if ((UINTPTR)topStack & (LOSCFG_STACK_POINT_ALIGN_SIZE - 1)) {
        return LOS_ERRNO_TSK_STKSZ_NOT_ALIGN;
    }

    if (initParam->uwStackSize & (LOSCFG_STACK_POINT_ALIGN_SIZE - 1)) {
        return LOS_ERRNO_TSK_STKSZ_NOT_ALIGN;
    }

    minStackSize = sizeof(TaskContext);
    if ((initParam->uwStackSize < KERNEL_TSK_MIN_STACK_SIZE) ||
        (initParam->uwStackSize < minStackSize)) {
        return LOS_ERRNO_TSK_STKSZ_TOO_SMALL;
    }

    if (initParam->uwStackSize > ((UINTPTR)(-1) - (UINTPTR)topStack)) {
        return LOS_ERRNO_TSK_STKSZ_TOO_LARGE;
    }
    return LOS_OK;
}
#endif

#ifdef LOSCFG_TASK_STACK_DYNAMIC_ALLOCATION
STATIC UINT32 OsTaskCreateParamCheck(const UINT32 *taskId, TSK_INIT_PARAM_S *initParam)
{
    UINT32 minStackSize;
    UINT32 ret;

    if (taskId == NULL) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    ret = OsTaskInitParamCheck(initParam);
    if (ret != LOS_OK) {
        return ret;
    }

    if (initParam->uwStackSize == 0) {
        initParam->uwStackSize = KERNEL_TSK_DEFAULT_STACK_SIZE;
    }

    if (initParam->uwStackSize > LOSCFG_SYS_HEAP_SIZE) {
        return LOS_ERRNO_TSK_STKSZ_TOO_LARGE;
    }

    minStackSize = sizeof(TaskContext);
    if ((initParam->uwStackSize < KERNEL_TSK_MIN_STACK_SIZE) ||
        (initParam->uwStackSize < minStackSize)) {
        return LOS_ERRNO_TSK_STKSZ_TOO_SMALL;
    }
    return LOS_OK;
}

STATIC VOID *OsTaskStackAlloc(UINT32 stackSize)
{
#if (LOSCFG_EXC_HARDWARE_STACK_PROTECTION == 1)
    return LOS_MemAllocAlign(OS_TASK_STACK_ADDR, stackSize, OS_TASK_STACK_PROTECT_SIZE);
#else
    return LOS_MemAllocAlign(OS_TASK_STACK_ADDR, stackSize, LOSCFG_STACK_POINT_ALIGN_SIZE);
#endif
}
#endif /* LOSCFG_TASK_STACK_DYNAMIC_ALLOCATION */

#ifdef LOSCFG_KERNEL_SMP_TASK_SYNC
STATIC INLINE UINT32 OsTaskSyncCreate(LosTaskCB *taskCB)
{
    UINT32 ret = LOS_SemCreate(0, &taskCB->syncSignal);
    if (ret != LOS_OK) {
        return LOS_ERRNO_TSK_MP_SYNC_RESOURCE;
    }
    return LOS_OK;
}

VOID OsTaskSyncDestroy(const LosTaskCB *taskCB)
{
    (VOID)LOS_SemDelete(taskCB->syncSignal);
}

STATIC INLINE UINT32 OsTaskSyncWait(const LosTaskCB *taskCB)
{
    UINT32 ret = LOS_OK;

    LOS_ASSERT(LOS_SpinHeld(&g_taskSpin));
    LOS_SpinUnlock(&g_taskSpin);
    /*
     * gc soft timer works every OS_MP_GC_PERIOD period, to prevent this timer
     * triggered right at the timeout has reached, we set the timeout as double
     * of the gc period.
     */
    if (LOS_SemPend(taskCB->syncSignal, OS_MP_GC_PERIOD * 2) != LOS_OK) {
        ret = LOS_ERRNO_TSK_MP_SYNC_FAILED;
    }

    LOS_SpinLock(&g_taskSpin);

    return ret;
}

STATIC INLINE VOID OsTaskSyncWake(const LosTaskCB *taskCB)
{
    UINT32 syncSignal = taskCB->syncSignal;
    /*
     * unlock to let synchronization works.
     * this operation needs task status set to UNUSED, and yet not put
     * back to the recycle or free tcb list.
     */
    LOS_ASSERT(OsTaskIsExit(taskCB));
    LOS_ASSERT(LOS_SpinHeld(&g_taskSpin));

    LOS_SpinUnlock(&g_taskSpin);

    /*
     * do the sync, because bottom half of LOS_SemPend has no operation on the sem,
     * we can delete this sem after the post.
     */
    (VOID)LOS_SemPost(syncSignal);
    (VOID)LOS_SemDelete(syncSignal);

    LOS_SpinLock(&g_taskSpin);
}
#else
STATIC INLINE UINT32 OsTaskSyncCreate(LosTaskCB *taskCB)
{
    (VOID)taskCB;
    return LOS_OK;
}

#ifdef LOSCFG_TASK_STACK_DYNAMIC_ALLOCATION
VOID OsTaskSyncDestroy(const LosTaskCB *taskCB)
{
    (VOID)taskCB;
}
#endif

#ifdef LOSCFG_KERNEL_SMP
STATIC INLINE UINT32 OsTaskSyncWait(const LosTaskCB *taskCB)
{
    (VOID)taskCB;
    return LOS_OK;
}
#endif

STATIC INLINE VOID OsTaskSyncWake(const LosTaskCB *taskCB)
{
    (VOID)taskCB;
}
#endif

LITE_OS_SEC_TEXT_INIT STATIC VOID OsTaskDelActionOnRun(LosTaskCB *taskCB)
{
    LosTaskCB *runTask = NULL;

    if (taskCB->usrStack) {
#ifdef LOSCFG_TASK_STACK_STATIC_ALLOCATION
        LOS_ListAdd(&g_losFreeTask, &taskCB->pendList);
#endif
    } else {
#ifdef LOSCFG_TASK_STACK_DYNAMIC_ALLOCATION
        LOS_ListTailInsert(&g_taskRecycleList, &taskCB->pendList);
#endif
    }

    runTask = &g_osTaskCBArray[g_taskMaxNum];
    runTask->taskId = taskCB->taskId;
    runTask->taskStatus = taskCB->taskStatus;
    runTask->topOfStack = taskCB->topOfStack;
    runTask->taskName = taskCB->taskName;

    taskCB->taskStatus = OS_TASK_STATUS_UNUSED;
    return;
}

#ifdef LOSCFG_KERNEL_SMP
LITE_OS_SEC_TEXT_MINOR VOID OsTaskProcSignal(VOID)
{
    LosTaskCB *runTask = NULL;
    UINT32    ret;

    /*
     * private and uninterruptible, no protection needed.
     * while this task is always running when others cores see it,
     * so it keeps receiving signals while follow code executing.
     */
    runTask = OsCurrTaskGet();
    if (runTask->mpSignal == SIGNAL_NONE) {
        return;
    }

    if (runTask->mpSignal & SIGNAL_KILL) {
        /*
         * clear the signal, and do the task deletion. if the signaled task has been
         * scheduled out, then this deletion will wait until next run.
         */
        runTask->mpSignal = SIGNAL_NONE;
        ret = LOS_TaskDelete(runTask->taskId);
        if (ret) {
            PRINT_ERR("%s: tsk del fail err:0x%x\n", __FUNCTION__, ret);
        }
    } else if (runTask->mpSignal & SIGNAL_SUSPEND) {
        runTask->mpSignal &= ~SIGNAL_SUSPEND;

        /* suspend killed task may fail, ignore the result */
        (VOID)LOS_TaskSuspend(runTask->taskId);
    } else if (runTask->mpSignal & SIGNAL_AFFI) {
        runTask->mpSignal &= ~SIGNAL_AFFI;

        OsSetSchedFlag(INT_PEND_RESCH);
        /* pri-queue has updated, notify the target cpu */
        LOS_MpSchedule((UINT32)runTask->cpuAffiMask);
#ifndef LOSCFG_POSIX_API
    }
#else
    } else if (runTask->mpSignal & SIGNAL_CANCEL) {
        /*
         * clear the signal, and do the task deletion. if the signaled task has been
         * scheduled out, then this deletion will wait until next run.
         */
        runTask->mpSignal = SIGNAL_NONE;
        ret = OsTaskDelete(runTask->taskId, (UINTPTR)OS_TASK_CANCEL_VALUE);
        if (ret) {
            PRINT_ERR("%s: tsk del fail err:0x%x\n", __FUNCTION__, ret);
        }
    }
#endif

    return;
}
#else
LITE_OS_SEC_TEXT_MINOR VOID OsTaskProcSignal(VOID)
{
}
#endif

/*
 * Check if needs to do the suspend operation in the current task.
 * Return TRUE, if needs to do the suspension.
 * Return FALSE, if meets following circumstances:
 * 1. Do the suspension across cores, if SMP is enabled
 * 2. Do the suspension when preemption is disabled
 * 3. Do the suspension in hard-irq
 * then LOS_TaskSuspend will directly return with 'ret' value.
 */

#ifndef LOSCFG_SCHED_LATENCY
STATIC INLINE UINT32 OsTaskUnlockVerify(UINT32 losTaskLock)
{
    if ((losTaskLock == 0) && ((OsPercpuGet()->schedFlag) == INT_PEND_RESCH) && OS_SCHEDULER_ACTIVE) {
        return LOS_OK;
    }

    return LOS_NOK;
}
#else
STATIC INLINE UINT32 OsTaskUnlockVerify(UINT32 losTaskLock)
{
    if ((losTaskLock == 0) && OS_SCHEDULER_ACTIVE) {
        return LOS_OK;
    }

    return LOS_NOK;
}
#endif

/*
 * Check if needs to do the delete operation in the current task.
 * Return TRUE, if needs to do the deletion.
 * Return FALSE, if meets following circumstances:
 * 1. Do the deletion across cores, if SMP is enabled
 * 2. Do the deletion when preemption is disabled
 * 3. Do the deletion in hard-irq
 * then LOS_TaskDelete will directly return with 'ret' value.
 */
LITE_OS_SEC_TEXT_INIT STATIC BOOL OsTaskDeleteCheckOnRun(LosTaskCB *taskCB, UINT32 *ret, UINTPTR retval)
{
#ifndef LOSCFG_KERNEL_SMP
    (VOID)retval;
#endif

    /* init default out return value */
    *ret = LOS_OK;

#ifdef LOSCFG_KERNEL_SMP
    /* ASYNCHRONIZED. No need to do task lock checking */
    if (taskCB->currCpu != ArchCurrCpuid()) {
        /*
         * the task is running on another cpu.
         * mask the target task with "kill/cancel" signal, and trigger mp schedule
         * which might not be essential but the deletion could more in time.
         */
#ifdef LOSCFG_POSIX_API
        OsTaskSignalSet(taskCB, (retval == (UINTPTR)OS_TASK_CANCEL_VALUE) ? SIGNAL_CANCEL : SIGNAL_KILL);
#else
        OsTaskSignalSet(taskCB, SIGNAL_KILL);
#endif
        LOS_MpSchedule(CPUID_TO_AFFI_MASK(taskCB->currCpu));
        *ret = OsTaskSyncWait(taskCB);
        return FALSE;
    }
#endif

    if (!OsPreemptableInSched()) {
        /* If the task is running and scheduler is locked then you can not delete it */
        *ret = LOS_ERRNO_TSK_DELETE_LOCKED;
        return FALSE;
    }

#ifdef LOSCFG_KERNEL_SMP
    if (OS_INT_ACTIVE) {
        /*
         * delete running task in interrupt.
         * mask "kill/cancel" signal and later deletion will be handled.
         */
#ifdef LOSCFG_POSIX_API
        OsTaskSignalSet(taskCB, (retval == (UINTPTR)OS_TASK_CANCEL_VALUE) ? SIGNAL_CANCEL : SIGNAL_KILL);
#else
        OsTaskSignalSet(taskCB, SIGNAL_KILL);
#endif
        return FALSE;
    }
#else
    (VOID)taskCB;
#endif

    return TRUE;
}

STATIC VOID OsTaskCBInit(LosTaskCB *taskCB, const TSK_INIT_PARAM_S *initParam, VOID *stackPtr,
    const VOID *topStack, BOOL useUsrStack)
{
#ifdef LOSCFG_KERNEL_TASK_ENTRY_VOID_PTR
    taskCB->args            = initParam->pArgs;
#else
    taskCB->arg             = initParam->uwArg;
#endif
    taskCB->stackSize       = initParam->uwStackSize;
    taskCB->taskSem         = NULL;
    taskCB->taskMux         = NULL;
    taskCB->priority        = initParam->usTaskPrio;
    taskCB->taskEntry       = initParam->pfnTaskEntry;
    taskCB->event.uwEventID = OS_NULL_INT;
    taskCB->eventMask       = 0;
    LOS_EventInit(&(taskCB->event));
    taskCB->taskName        = initParam->pcName;
    taskCB->msg             = NULL;
#if (LOSCFG_KERNEL_SIGNAL == 1)
    taskCB->sig             = NULL;
#endif

    SET_SORTLIST_VALUE(&taskCB->sortList, OS_SORT_LINK_INVALID_TIME);

#ifdef LOSCFG_TASK_JOINABLE
    taskCB->taskRetval = NULL;
    taskCB->joined = NULL;
    taskCB->joinner = NULL;
    if (initParam->uwResved & LOS_TASK_ATTR_JOINABLE) {
        taskCB->taskFlags = OS_TASK_FLAG_JOINABLE;
    } else {
        taskCB->taskFlags = OS_TASK_FLAG_DETACHED;
    }
#else
    taskCB->taskFlags = OS_TASK_FLAG_DETACHED;
#endif

    taskCB->usrStack     = useUsrStack ? 1 : 0; /* 0: dynamically alloc stack space;1: user inputs stack space */

    OsTaskSignalSet(taskCB, SIGNAL_NONE);

#ifdef LOSCFG_KERNEL_SMP
    taskCB->currCpu      = OS_TASK_INVALID_CPUID;
    taskCB->lastCpu      = OS_TASK_INVALID_CPUID;
    taskCB->cpuAffiMask  = (initParam->usCpuAffiMask) ? initParam->usCpuAffiMask : LOSCFG_KERNEL_CPU_MASK;
#endif
    taskCB->timeSlice       = 0;
    taskCB->waitTimes       = 0;

    if (useUsrStack == FALSE) {
        taskCB->stackSize = ALIGN(initParam->uwStackSize, OS_TASK_STACK_ADDR_ALIGN);
#if (LOSCFG_EXC_HARDWARE_STACK_PROTECTION == 1)
        taskCB->topOfStack = (UINTPTR)stackPtr + OS_TASK_STACK_PROTECT_SIZE;
#else
        taskCB->topOfStack = (UINTPTR)stackPtr;
#endif
    } else {
        taskCB->topOfStack = (UINTPTR)LOS_Align((UINTPTR)topStack, LOSCFG_STACK_POINT_ALIGN_SIZE);
        taskCB->stackSize = initParam->uwStackSize - (taskCB->topOfStack - (UINTPTR)topStack);
        taskCB->stackSize = TRUNCATE(taskCB->stackSize, OS_TASK_STACK_ADDR_ALIGN);
    }

    /* initialize the task stack, write magic num to stack top */
    (VOID)memset_s((VOID *)taskCB->topOfStack, taskCB->stackSize,
                   (INT32)(OS_TASK_STACK_INIT & 0xFF), taskCB->stackSize);

    *((UINT32 *)taskCB->topOfStack) = OS_TASK_MAGIC_WORD;
    taskCB->stackPointer = ArchTaskStackInit(taskCB->taskId, taskCB->stackSize, (VOID *)taskCB->topOfStack);
}

STATIC UINT32 OsTaskGetFreeTaskCB(LosTaskCB **taskCB)
{
    if (LOS_ListEmpty(&g_losFreeTask)) {
        return LOS_ERRNO_TSK_TCB_UNAVAILABLE;
    }

    *taskCB = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(&g_losFreeTask));
    if (*taskCB == NULL) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }
    LOS_ListDelete(LOS_DL_LIST_FIRST(&g_losFreeTask));
    return LOS_OK;
}

STATIC UINT32 OsTaskCreateOnly(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack, BOOL useUsrStack)
{
    UINT32 intSave;
    UINT32 errRet = LOS_OK;
    VOID *stackPtr = NULL;
    LosTaskCB *taskCB = NULL;

    if (useUsrStack) {
#ifdef LOSCFG_TASK_STACK_STATIC_ALLOCATION
        errRet = OsTaskCreateParamCheckStatic(taskId, initParam, topStack);
#endif
    } else {
#ifdef LOSCFG_TASK_STACK_DYNAMIC_ALLOCATION
        errRet = OsTaskCreateParamCheck(taskId, initParam);
#endif
    }
    if (errRet != LOS_OK) {
        return errRet;
    }

    LOS_TaskResRecycle();

    SCHEDULER_LOCK(intSave);
    errRet = OsTaskGetFreeTaskCB(&taskCB);
    if (errRet != LOS_OK) {
        goto LOS_ERREND;
    }
    SCHEDULER_UNLOCK(intSave);

    errRet = OsTaskSyncCreate(taskCB);
    if (errRet != LOS_OK) {
        goto LOS_ERREND_REWIND_TCB;
    }

    if (useUsrStack == FALSE) {
#ifdef LOSCFG_TASK_STACK_DYNAMIC_ALLOCATION
        UINT32 stackSize = ALIGN(initParam->uwStackSize, OS_TASK_STACK_ADDR_ALIGN);
#if (LOSCFG_EXC_HARDWARE_STACK_PROTECTION == 1)
        stackSize += OS_TASK_STACK_PROTECT_SIZE;
#endif
        stackPtr = OsTaskStackAlloc(stackSize);
        if (stackPtr == NULL) {
            errRet = LOS_ERRNO_TSK_NO_MEMORY;
            OsTaskSyncDestroy(taskCB);
            goto LOS_ERREND_REWIND_TCB;
        }
#endif
    }

    OsTaskCBInit(taskCB, initParam, stackPtr, topStack, useUsrStack);

    /*
     * The suspend state must be set after the taskCB is fully initialized in case the task is resumed
     * when taskCB is not ready.
     */
    taskCB->taskStatus = OS_TASK_STATUS_SUSPEND;

    LOSCFG_TASK_CREATE_EXTENSION_HOOK(taskCB);

#if (LOSCFG_BASE_CORE_CPUP == 1)
    SCHEDULER_LOCK(intSave);
    OsCpupCBTaskCreate(taskCB->taskId, taskCB->taskStatus);
    SCHEDULER_UNLOCK(intSave);
#endif
    *taskId = taskCB->taskId;
    OsHookCall(LOS_HOOK_TYPE_TASK_CREATE, taskCB);
    return LOS_OK;

LOS_ERREND_REWIND_TCB:
    SCHEDULER_LOCK(intSave);
    LOS_ListAdd(&g_losFreeTask, &taskCB->pendList);
LOS_ERREND:
    SCHEDULER_UNLOCK(intSave);
    return errRet;
}
#ifdef LOSCFG_TASK_STACK_STATIC_ALLOCATION
UINT32 LOS_TaskCreateOnlyStatic(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack)
{
    if ((initParam == NULL) || (topStack == NULL)) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }
    return OsTaskCreateOnly(taskId, initParam, topStack, TRUE);
}

UINT32 LOS_TaskCreateStatic(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack)
{
    UINT32 ret;

    ret = LOS_TaskCreateOnlyStatic(taskId, initParam, topStack);
    if (ret != LOS_OK) {
        return ret;
    }

    return LOS_TaskResume(*taskId);
}
#endif /* LOSCFG_TASK_STACK_STATIC_ALLOCATION */

LITE_OS_SEC_TEXT_INIT UINT32 LOS_TaskCreateOnly(UINT32 *taskId, TSK_INIT_PARAM_S *initParam)
{
    /* Static stacks must go through LOS_TaskCreateStatic. To re-enable
     * stackAddr passing: re-add the TSK_INIT_PARAM_S::stackAddr field and
     * insert before the return below:
     *   if (initParam->stackAddr != (UINTPTR)NULL)
     *       return OsTaskCreateOnly(taskId, initParam,
     *                               (VOID *)(UINTPTR)initParam->stackAddr, TRUE); */
    return OsTaskCreateOnly(taskId, initParam, NULL, FALSE);
}

LITE_OS_SEC_TEXT_INIT UINT32 LOS_TaskCreate(UINT32 *taskId, TSK_INIT_PARAM_S *initParam)
{
    UINT32 ret;

    ret = LOS_TaskCreateOnly(taskId, initParam);
    if (ret != LOS_OK) {
        return ret;
    }

    return LOS_TaskResume(*taskId);
}

LITE_OS_SEC_TEXT_INIT STATIC INLINE VOID OsTaskResourceReclaim(LosTaskCB *taskCB, UINT32 *intSave)
{
    (VOID)intSave;

#ifdef LOSCFG_BASE_IPC_EVENT
    taskCB->event.uwEventID = OS_NULL_INT;
    taskCB->eventMask = 0;
#endif
#if (LOSCFG_BASE_CORE_CPUP == 1)
    OsCpupCBTaskDelete(taskCB->taskId);
#endif
#if (LOSCFG_KERNEL_SIGNAL == 1)
    if (taskCB->sig != NULL) {
        LOS_MemFree(OS_SYS_MEM_ADDR, taskCB->sig);
        taskCB->sig = NULL;
    }
#endif
    LOSCFG_TASK_DELETE_EXTENSION_HOOK(taskCB);

    OsTaskSyncWake(taskCB);
}

LITE_OS_SEC_TEXT_INIT UINT32 OsTaskDelete(UINT32 taskId, UINTPTR retVal)
{
    LosTaskCB *taskCB = NULL;
    UINT32 intSave;
    UINT32 errRet = LOS_OK;
    UINT16 tempStatus;

    taskCB = OS_TCB_FROM_TID(taskId);
    if (taskCB->taskFlags & OS_TASK_FLAG_SYSTEM) {
        return LOS_ERRNO_TSK_OPERATE_SYSTEM_TASK;
    }

    SCHEDULER_LOCK(intSave);

    tempStatus = taskCB->taskStatus;
    if (tempStatus & OS_TASK_STATUS_UNUSED) {
        errRet = LOS_ERRNO_TSK_NOT_CREATED;
        goto LOS_UNLOCK;
    }

    if (OS_TASK_IS_ZOMBIE(tempStatus)) {
        errRet = LOS_ERRNO_TSK_IS_ZOMBIE;
        goto LOS_UNLOCK;
    }

    if (tempStatus & OS_TASK_FLAG_SIGNAL) {
        errRet = LOS_ERRNO_TSK_PROCESS_SIGNAL;
        goto LOS_UNLOCK;
    }

    if (tempStatus & OS_TASK_STATUS_RUNNING) {
        if (!OsTaskDeleteCheckOnRun(taskCB, &errRet, retVal)) {
            goto LOS_UNLOCK;
        }
    }

    OsHookCall(LOS_HOOK_TYPE_TASK_DELETE, taskCB);

    if (tempStatus & OS_TASK_STATUS_READY) {
        (VOID)OsSchedReadyTaskRemove(taskCB);
    } else if ((tempStatus & OS_TASK_STATUS_PEND) && (!OS_TASK_IS_JOINING(taskCB))) {
        LOS_ListDelete(&taskCB->pendList);
        taskCB->taskStatus &= ~OS_TASK_STATUS_PEND;
    }

#ifdef LOSCFG_TASK_JOINABLE
    OsTaskCancelJoin(taskCB);
#endif

    if (taskCB->taskStatus & (OS_TASK_STATUS_DELAY | OS_TASK_STATUS_PEND_TIME)) {
        OsTimerListDelete(taskCB);
        taskCB->taskStatus &= ~(OS_TASK_STATUS_DELAY | OS_TASK_STATUS_PEND_TIME);
    }

    taskCB->taskStatus &= ~OS_TASK_STATUS_SUSPEND;

    if (OS_TASK_IS_JOINABLE(taskCB)) {
        taskCB->taskStatus |= OS_TASK_STATUS_ZOMBIE;
    } else {
        taskCB->taskStatus |= OS_TASK_STATUS_UNUSED;
        OsTaskResourceReclaim(taskCB, &intSave);
    }

#ifdef LOSCFG_TASK_JOINABLE
    if (OS_TASK_IS_JOINABLE(taskCB)) {
        OsTaskJoinPost(taskCB);
        taskCB->taskRetval = (VOID *)retVal;
    }
#endif

    if (taskCB->taskStatus & OS_TASK_STATUS_RUNNING) {
        if (OS_TASK_IS_DETACHED(taskCB)) {
            OsTaskDelActionOnRun(taskCB);
        }

        OsTaskReSched();
        SCHEDULER_UNLOCK(intSave);
        goto LOS_RETURN;
    } else if (OS_TASK_IS_DETACHED(taskCB)) {
        taskCB->taskStatus = OS_TASK_STATUS_UNUSED;
        OsTaskStackFree(taskCB);
    }

LOS_UNLOCK:
    SCHEDULER_UNLOCK(intSave);

LOS_RETURN:
    return errRet;
}

/*****************************************************************************
 Function    : LOS_TaskDelete
 Description : Delete a task
 Input       : taskId    --- Task ID
 Output      : None
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
LITE_OS_SEC_TEXT_INIT UINT32 LOS_TaskDelete(UINT32 taskId)
{
    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

#ifdef LOSCFG_TASK_JOINABLE
    return OsTaskDelete(taskId, (UINTPTR)(OsCurrTaskGet()->taskId));
#else
    return OsTaskDelete(taskId, 0);
#endif
}

/*****************************************************************************
 Function    : LOS_TaskResume
 Description : Resume suspend task
 Input       : taskId --- Task ID
 Output      : None
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
LITE_OS_SEC_TEXT_INIT UINT32 LOS_TaskResume(UINT32 taskId)
{
    UINT32 intSave;
    LosTaskCB *taskCB = NULL;
    UINT16 tempStatus;
    UINT32 errRet;

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    SCHEDULER_LOCK(intSave);
#ifdef LOSCFG_KERNEL_SMP
    /* clear pending signal */
    taskCB->mpSignal &= ~SIGNAL_SUSPEND;
#endif
    tempStatus = taskCB->taskStatus;

    if (tempStatus & OS_TASK_STATUS_UNUSED) {
        errRet = LOS_ERRNO_TSK_NOT_CREATED;
        OS_GOTO_ERREND();
    } else if (OS_TASK_IS_ZOMBIE(tempStatus)) {
        errRet = LOS_ERRNO_TSK_IS_ZOMBIE;
        OS_GOTO_ERREND();
    } else if (!(tempStatus & OS_TASK_STATUS_SUSPEND)) {
        errRet = LOS_ERRNO_TSK_NOT_SUSPENDED;
        OS_GOTO_ERREND();
    }

#if (LOSCFG_KERNEL_PM == 1)
    if (taskCB->taskStatus & OS_TASK_FLAG_FREEZE) {
        OsSchedUnfreezeTask(taskCB);
    }
#endif

    taskCB->taskStatus &= ~OS_TASK_STATUS_SUSPEND;
    if (taskCB->taskStatus & OS_CHECK_TASK_BLOCK) {
        errRet = LOS_OK;
        OS_GOTO_ERREND();
    }

    OsSchedResume(taskCB, intSave);
    if (OS_SCHEDULER_ACTIVE) {
        LOS_MpSchedule(OS_MP_CPU_ALL);
        LOS_Schedule();
    }
    return LOS_OK;

LOS_ERREND:
    SCHEDULER_UNLOCK(intSave);
    return errRet;
}

/*****************************************************************************
 Function    : LOS_TaskSuspend
 Description : Suspend task
 Input       : taskId --- Task ID
 Output      : None
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
LITE_OS_SEC_TEXT_INIT UINT32 LOS_TaskSuspend(UINT32 taskId)
{
    LosTaskCB *taskCB = NULL;

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    if (taskCB->taskFlags & OS_TASK_FLAG_SYSTEM) {
        return LOS_ERRNO_TSK_OPERATE_SYSTEM_TASK;
    }

    return OsSchedSuspend(taskCB);
}

/*****************************************************************************
 Function    : LOS_TaskDelay
 Description : delay the current task
 Input       : tick    --- time
 Output      : None
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
LITE_OS_SEC_TEXT UINT32 LOS_TaskDelay(UINT32 tick)
{
    UINT32 intSave;
    LosTaskCB *runTask;

    /* TODO: Temporary manual interception */
    if (!OS_SCHEDULER_ACTIVE) {
        return LOS_OK;
    }

    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_TSK_DELAY_IN_INT;
    }

    runTask = OsCurrTaskGet();
    if (runTask->taskFlags & OS_TASK_FLAG_SYSTEM) {
        return LOS_ERRNO_TSK_OPERATE_SYSTEM_TASK;
    }

    if (!OsPreemptable()) {
        return LOS_ERRNO_TSK_DELAY_IN_LOCK;
    }
    OsHookCall(LOS_HOOK_TYPE_TASK_DELAY, tick);
    if (tick == 0) {
        return LOS_TaskYield();
    } else {
        SCHEDULER_LOCK(intSave);
        OsTaskAdd2TimerList(runTask, tick);
        runTask->taskStatus |= OS_TASK_STATUS_DELAY;
        runTask->waitTimes = tick;
        OsHookCall(LOS_HOOK_TYPE_MOVEDTASKTODELAYEDLIST, runTask);
        OsSchedResched();
        SCHEDULER_UNLOCK(intSave);
    }

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT16 LOS_TaskPriGet(UINT32 taskId)
{
    UINT32 intSave;
    LosTaskCB *taskCB = NULL;
    UINT16 priority;

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return (UINT16)OS_INVALID;
    }

    taskCB = OS_TCB_FROM_TID(taskId);

    SCHEDULER_LOCK(intSave);
    if (taskCB->taskStatus & OS_TASK_STATUS_UNUSED) {
        priority = (UINT16)OS_INVALID;
    } else {
        priority = taskCB->priority;
    }

    SCHEDULER_UNLOCK(intSave);
    return priority;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_TaskPriSet(UINT32 taskId, UINT16 taskPrio)
{
    BOOL isReady = FALSE;
    UINT32 intSave;
    LosTaskCB *taskCB = NULL;
    UINT16 tempStatus;

    if (taskPrio > LOS_TASK_PRIORITY_LOWEST) {
        return LOS_ERRNO_TSK_PRIOR_ERROR;
    }

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    SCHEDULER_LOCK(intSave);
    tempStatus = taskCB->taskStatus;
    if (tempStatus & OS_TASK_STATUS_UNUSED) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_TSK_NOT_CREATED;
    }
    if (tempStatus & OS_TASK_STATUS_ZOMBIE) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_TSK_IS_ZOMBIE;
    }
    if (taskCB->taskFlags & OS_TASK_FLAG_SYSTEM) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_TSK_OPERATE_SYSTEM_TASK;
    }

    isReady = OsSchedPrioModify(taskCB, taskPrio);
    SCHEDULER_UNLOCK(intSave);
    /* delete the task and insert with right priority into ready queue */
    if (isReady) {
        LOS_MpSchedule(OS_MP_CPU_ALL);
        LOS_Schedule();
    }

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_CurTaskPriSet(UINT16 taskPrio)
{
    return LOS_TaskPriSet(OsCurrTaskGet()->taskId, taskPrio);
}

/*****************************************************************************
 Function    : LOS_TaskYield
 Description : Adjust the procedure order of specified task
 Input       : None
 Output      : None
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
LITE_OS_SEC_TEXT_MINOR UINT32 LOS_TaskYield(VOID)
{
    UINT32 intSave;
    LosTaskCB *runTask = NULL;
    UINT32 ret;

    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_TSK_YIELD_IN_INT;
    }

    if (!OsPreemptable()) {
        return LOS_ERRNO_TSK_YIELD_IN_LOCK;
    }

    runTask = OsCurrTaskGet();
    if (runTask->taskId >= g_taskMaxNum) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    SCHEDULER_LOCK(intSave);
    ret = OsSchedYield();
    if (ret == LOS_OK) {
        OsSchedResched();
    }
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

BOOL LOS_TaskIsScheduled(VOID)
{
    return OS_SCHEDULER_ACTIVE;
}

/*****************************************************************************
 Function    : LOS_NewTaskIDGet
 Description : get id of current new task.
 Input       : None
 Output      : None
 Return      : task id
 *****************************************************************************/
LITE_OS_SEC_TEXT UINT32 LOS_NewTaskIDGet(VOID)
{
    return LOS_NextTaskIDGet();
}

/*****************************************************************************
 Function    : LOS_TaskNameGet
 Description : get Name of current new task.
 Input       : taskId -----task id
 Output      : None
 Return      : task name
 *****************************************************************************/
LITE_OS_SEC_TEXT CHAR* LOS_TaskNameGet(UINT32 taskId)
{
    UINT32    intSave;
    LosTaskCB *taskCB = NULL;

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return NULL;
    }

    taskCB = OS_TCB_FROM_TID(taskId);

    SCHEDULER_LOCK(intSave);
    if (taskCB->taskStatus & OS_TASK_STATUS_UNUSED) {
        SCHEDULER_UNLOCK(intSave);
        return NULL;
    }
    SCHEDULER_UNLOCK(intSave);

    return taskCB->taskName;
}

LITE_OS_SEC_TEXT_MINOR VOID LOS_Msleep(UINT32 mSecs)
{
    UINT32 interval;

    if (OS_INT_ACTIVE) {
        return;
    }

    if (mSecs == 0) {
        interval = 0;
    } else {
        interval = LOS_MS2Tick(mSecs);
        if (interval == 0) {
            interval = 1;
        }
    }

    (VOID)LOS_TaskDelay(interval);
}

/*****************************************************************************
 Function    : LOS_TaskLock
 Description : Task lock
 Input       : None
 Output      : None
 Return      : None
 *****************************************************************************/
LITE_OS_SEC_TEXT_MINOR VOID LOS_TaskLock(VOID)
{
    UINT32 intSave;
    UINT32 *losTaskLock = NULL;

    intSave = LOS_IntLock();
    losTaskLock = &OsPercpuGet()->taskLockCnt;
    (*losTaskLock)++;
    LOS_IntRestore(intSave);
}

/*****************************************************************************
 Function    : LOS_TaskUnlock
 Description : Task unlock
 Input       : None
 Output      : None
 Return      : None
 *****************************************************************************/
LITE_OS_SEC_TEXT_MINOR VOID LOS_TaskUnlock(VOID)
{
    UINT32 intSave;
    UINT32 *losTaskLock = NULL;
    Percpu *percpu = NULL;

    intSave = LOS_IntLock();

    percpu = OsPercpuGet();
    losTaskLock = &percpu->taskLockCnt;
    if (*losTaskLock > 0) {
        (*losTaskLock)--;
        if (OsTaskUnlockVerify(*losTaskLock) == LOS_OK) {
            OsSetSchedFlag(INT_NO_RESCH);
            LOS_IntRestore(intSave);
            LOS_Schedule();
            return;
        }
    }

    LOS_IntRestore(intSave);
}

/*****************************************************************************
 Function    : LOS_TaskUnlockNoSched
 Description : Decrement task scheduling lock count without triggering schedule.
 Input       : None
 Output      : None
 Return      : None
 *****************************************************************************/
LITE_OS_SEC_TEXT_MINOR VOID LOS_TaskUnlockNoSched(VOID)
{
    UINT32 intSave;
    UINT32 *losTaskLock = NULL;
    Percpu *percpu = NULL;

    intSave = LOS_IntLock();

    percpu = OsPercpuGet();
    losTaskLock = &percpu->taskLockCnt;
    if (*losTaskLock > 0) {
        (*losTaskLock)--;
    }

    LOS_IntRestore(intSave);
}

STATIC INLINE UINT32 OsGetSemID(const LosTaskCB *taskCB)
{
    UINT32 semId = LOSCFG_BASE_IPC_SEM_LIMIT;

    if (taskCB->taskSem != NULL) {
        semId = ((LosSemCB *)taskCB->taskSem)->semId;
    }

    return semId;
}

STATIC INLINE UINT32 OsGetMuxID(const LosTaskCB *taskCB)
{
    UINT32 muxId = LOSCFG_BASE_IPC_MUX_LIMIT;

    if (taskCB->taskMux != NULL) {
        muxId = ((LosMuxCB *)taskCB->taskMux)->muxId;
    }

    return muxId;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_TaskInfoGet(UINT32 taskId, TSK_INFO_S *taskInfo)
{
    UINT32 intSave;
    LosTaskCB *taskCB = NULL;

    if (taskInfo == NULL) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    SCHEDULER_LOCK(intSave);

    if (taskCB->taskStatus & OS_TASK_STATUS_UNUSED) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_TSK_NOT_CREATED;
    }

    taskInfo->uwSP = (UINTPTR)taskCB->stackPointer;
    taskInfo->usTaskStatus = taskCB->taskStatus;
    taskInfo->usTaskPrio = taskCB->priority;
    taskInfo->uwStackSize = taskCB->stackSize;
    taskInfo->uwTopOfStack = taskCB->topOfStack;
    taskInfo->uwEvent = taskCB->event;
    taskInfo->uwEventMask = taskCB->eventMask;
    taskInfo->uwSemID = OsGetSemID(taskCB);
    taskInfo->uwMuxID = OsGetMuxID(taskCB);
    taskInfo->pTaskSem = taskCB->taskSem;
    taskInfo->pTaskMux = taskCB->taskMux;
    taskInfo->uwTaskID = taskId;
    // Ignore the return code when matching CSEC rule 6.6(4).
    (VOID)strncpy_s(taskInfo->acName, LOS_TASK_NAMELEN, taskCB->taskName, LOS_TASK_NAMELEN - 1);
    taskInfo->acName[LOS_TASK_NAMELEN - 1] = '\0';

    taskInfo->uwBottomOfStack = TRUNCATE((taskCB->topOfStack + taskCB->stackSize),
                                         OS_TASK_STACK_ADDR_ALIGN);
    taskInfo->uwCurrUsed = taskInfo->uwBottomOfStack - taskInfo->uwSP;
    taskInfo->uwPeakUsed = OsGetTaskWaterLine(taskId);
    taskInfo->bOvf = (taskInfo->uwPeakUsed == OS_NULL_INT) ? TRUE : FALSE;
    SCHEDULER_UNLOCK(intSave);

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_TaskStatusGet(UINT32 taskId, UINT32 *taskStatus)
{
    UINT32    intSave;
    LosTaskCB *taskCB = NULL;

    if (taskStatus == NULL) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    SCHEDULER_LOCK(intSave);

    if (taskCB->taskStatus & OS_TASK_STATUS_UNUSED) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_TSK_NOT_CREATED;
    }

    *taskStatus = taskCB->taskStatus;

    SCHEDULER_UNLOCK(intSave);

    return LOS_OK;
}

#if (LOSCFG_BASE_CORE_EXC_TSK_SWITCH == 1)
LITE_OS_SEC_TEXT_MINOR UINT32 LOS_TaskSwitchInfoGet(UINT32 index, UINT32 *taskSwitchInfo)
{
    UINT32 intSave;
    UINT32 curIndex;

    curIndex = index;
    if (curIndex >= OS_TASK_SWITCH_INFO_COUNT) {
        curIndex %= OS_TASK_SWITCH_INFO_COUNT;
    }

    if (taskSwitchInfo == NULL) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }

    SCHEDULER_LOCK(intSave);

    (*taskSwitchInfo) = g_taskSwitchInfo.pid[curIndex];

    if (memcpy_s((VOID *)(taskSwitchInfo + 1), LOS_TASK_NAMELEN,
                 g_taskSwitchInfo.name[curIndex], LOS_TASK_NAMELEN) != EOK) {
        PRINT_ERR("LOS_TaskSwitchInfoGet copy task name failed\n");
    }

    SCHEDULER_UNLOCK(intSave);
    return LOS_OK;
}
#endif

/*****************************************************************************
Function    : LOS_TaskInfoMonitor
Description : Get all task info
Input       : None
Return      : LOS_OK on success ,or OS_ERROR on failure
*****************************************************************************/
LITE_OS_SEC_TEXT_MINOR UINT32 LOS_TaskInfoMonitor(VOID)
{
    UINT32 retVal;

    retVal = OsGetAllTskInfo();

    return retVal;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_TaskCpuAffiSet(UINT32 taskId, UINT16 cpuAffiMask)
{
#ifdef LOSCFG_KERNEL_SMP
    LosTaskCB *taskCB = NULL;
    UINT32 intSave;
    BOOL needSched = FALSE;
    UINT16 currCpuMask;

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    if (!(cpuAffiMask & LOSCFG_KERNEL_CPU_MASK)) {
        return LOS_ERRNO_TSK_CPU_AFFINITY_MASK_ERR;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    if (taskCB->taskFlags & OS_TASK_FLAG_SYSTEM) {
        return LOS_ERRNO_TSK_OPERATE_SYSTEM_TASK;
    }

    SCHEDULER_LOCK(intSave);

    if (taskCB->taskStatus & OS_TASK_STATUS_UNUSED) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_TSK_NOT_CREATED;
    }

    if (OS_TASK_IS_ZOMBIE(taskCB->taskStatus)) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_TSK_IS_ZOMBIE;
    }

    taskCB->cpuAffiMask = cpuAffiMask;
    currCpuMask = CPUID_TO_AFFI_MASK(taskCB->currCpu);
    if (!(currCpuMask & cpuAffiMask)) {
        needSched = TRUE;
        OsSchedAffiChange(taskCB);
        OsTaskSignalSet(taskCB, SIGNAL_AFFI);
    }
    SCHEDULER_UNLOCK(intSave);

    if (needSched && OS_SCHEDULER_ACTIVE) {
        LOS_MpSchedule(currCpuMask);
        LOS_Schedule();
    }
    return LOS_OK;

#else
    (VOID)taskId;
    (VOID)cpuAffiMask;
    return LOS_OK;
#endif
}

LITE_OS_SEC_TEXT_MINOR UINT16 LOS_TaskCpuAffiGet(UINT32 taskId)
{
#ifdef LOSCFG_KERNEL_SMP
#define INVALID_CPU_AFFI_MASK   0
    LosTaskCB *taskCB = NULL;
    UINT16 cpuAffiMask;
    UINT32 intSave;

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return INVALID_CPU_AFFI_MASK;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    SCHEDULER_LOCK(intSave);
    if (taskCB->taskStatus & OS_TASK_STATUS_UNUSED) {
        SCHEDULER_UNLOCK(intSave);
        return INVALID_CPU_AFFI_MASK;
    }

    cpuAffiMask = taskCB->cpuAffiMask;
    SCHEDULER_UNLOCK(intSave);

    return cpuAffiMask;
#else
    (VOID)taskId;
    return 1; /* 1: mask of current cpu */
#endif
}

LITE_OS_SEC_TEXT_INIT UINT32 LOS_TaskAllocSecureContext(UINT32 taskId, UINT32 size)
{
    (VOID)taskId;
    (VOID)size;
    return LOS_OK;
}

#ifdef LOSCFG_TASK_JOINABLE
LITE_OS_SEC_TEXT_INIT STATIC INLINE UINT32 OsTaskJoinPend(LosTaskCB *taskCB, UINTPTR *retval, UINT32 *intSave)
{
    LOS_ASSERT(LOS_SpinHeld(&g_taskSpin));

    if (!OS_TASK_IS_ZOMBIE(taskCB->taskStatus)) {
        LosTaskCB *runTask = OsCurrTaskGet();

        LOS_ASSERT(runTask->joined == NULL);
        runTask->taskStatus |= OS_TASK_STATUS_PEND;
        runTask->joined = taskCB;
        taskCB->joinner = runTask;
        OsSchedResched();
        runTask->joined = NULL;
        taskCB->joinner = NULL;
    }

    if (retval != NULL) {
        *retval = (UINTPTR)taskCB->taskRetval;
    }
    taskCB->taskStatus = OS_TASK_STATUS_UNUSED;
    OsTaskResourceReclaim(taskCB, intSave);
    OsTaskStackFree(taskCB);
    return LOS_OK;
}

LITE_OS_SEC_TEXT_INIT UINT32 LOS_TaskJoin(UINT32 taskId, UINTPTR *retval)
{
    UINT32 ret;
    UINT32 intSave;
    LosTaskCB *taskCB = NULL;

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_TSK_NOT_ALLOW_IN_INT;
    }

    if (!OsPreemptable()) {
        return LOS_ERRNO_TSK_SCHED_LOCKED;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    if (taskCB == OsCurrTaskGet()) {
        return LOS_ERRNO_TSK_NOT_JOIN_SELF;
    }

    if (taskCB->taskFlags & OS_TASK_FLAG_SYSTEM) {
        return LOS_ERRNO_TSK_OPERATE_SYSTEM_TASK;
    }

    SCHEDULER_LOCK(intSave);
    if (taskCB->taskStatus & OS_TASK_STATUS_UNUSED) {
        ret = LOS_ERRNO_TSK_NOT_CREATED;
        goto LOS_UNLOCK;
    }

    if (OS_TASK_IS_DETACHED(taskCB)) {
        ret = LOS_ERRNO_TSK_IS_DETACHED;
        goto LOS_UNLOCK;
    }

    if (OS_TASK_IS_ALREADY_JOIN(taskCB)) {
        ret =  LOS_ERRNO_TSK_ALREADY_JOIN;
        goto LOS_UNLOCK;
    }

    ret = OsTaskJoinPend(taskCB, retval, &intSave);

LOS_UNLOCK:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

LITE_OS_SEC_TEXT_INIT UINT32 LOS_TaskDetach(UINT32 taskId)
{
    LosTaskCB *taskCB = NULL;
    UINT32 intSave;
    UINT32 ret = LOS_OK;

    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_TSK_NOT_ALLOW_IN_INT;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    if (taskCB->taskFlags & OS_TASK_FLAG_SYSTEM) {
        return LOS_ERRNO_TSK_OPERATE_SYSTEM_TASK;
    }

    SCHEDULER_LOCK(intSave);
    if (taskCB->taskStatus & OS_TASK_STATUS_UNUSED) {
        ret = LOS_ERRNO_TSK_NOT_CREATED;
        goto LOS_UNLOCK;
    }

    if (OS_TASK_IS_DETACHED(taskCB)) {
        ret = LOS_ERRNO_TSK_IS_DETACHED;
        goto LOS_UNLOCK;
    }

    if (OS_TASK_IS_ALREADY_JOIN(taskCB)) {
        ret =  LOS_ERRNO_TSK_ALREADY_JOIN;
        goto LOS_UNLOCK;
    }

    if (taskCB->taskStatus & OS_TASK_STATUS_ZOMBIE) {
        ret = OsTaskJoinPend(taskCB, NULL, &intSave);
        goto LOS_UNLOCK;
    }

    taskCB->taskFlags |= OS_TASK_FLAG_DETACHED;

LOS_UNLOCK:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}
#endif /* LOSCFG_TASK_JOINABLE */

