/* ----------------------------------------------------------------------------
 * Copyright (c) 2013-2022 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: Schedule Statistics Debug
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
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
 * --------------------------------------------------------------------------- */

#include "los_sched_debug_pri.h"
#include "los_debug.h"
#include "los_tick.h"
#include "los_config.h"
#include "securec.h"

#ifdef LOSCFG_DEBUG_SCHED_STATISTICS

#define HIGHTASKPRI           16
#define NS_PER_MS             1000000
#define DECIMAL_TO_PERCENTAGE 100

typedef struct {
    UINT64      idleRuntime;
    UINT64      idleStarttime;
    UINT64      highTaskRuntime;
    UINT64      highTaskStarttime;
    UINT64      sumPriority;
    UINT32      prioritySwitch;
    UINT32      highTaskSwitch;
    UINT32      contextSwitch;
    UINT32      hwiNum;
} StatPercpu;

STATIC BOOL g_statisticsStartFlag = FALSE;
STATIC UINT64 g_statisticsStartTime;
STATIC UINT64 g_statisticsPastTime = 0;
STATIC StatPercpu g_statPercpu[LOSCFG_KERNEL_CORE_NUM];

STATIC VOID OsSchedStatPerCpu(const LosTaskCB *runTask, const LosTaskCB *newTask, UINT64 currTime)
{
    UINT32 idleTaskId;
    UINT64 runtime;

    idleTaskId = OsGetIdleTaskId();

    g_statPercpu[0].contextSwitch++;

    if ((runTask->taskId != idleTaskId) && (newTask->taskId == idleTaskId)) {
        g_statPercpu[0].idleStarttime = currTime;
    }

    if ((runTask->taskId == idleTaskId) && (newTask->taskId != idleTaskId)) {
        runtime = currTime - g_statPercpu[0].idleStarttime;
        g_statPercpu[0].idleRuntime += runtime;
        g_statPercpu[0].idleStarttime = 0;
    }

    if ((runTask->priority >= HIGHTASKPRI) && (newTask->priority < HIGHTASKPRI)) {
        g_statPercpu[0].highTaskStarttime = currTime;
    }

    if ((runTask->priority < HIGHTASKPRI) && (newTask->priority >= HIGHTASKPRI)) {
        runtime = currTime - g_statPercpu[0].highTaskStarttime;
        g_statPercpu[0].highTaskRuntime += runtime;
        g_statPercpu[0].highTaskStarttime = 0;
    }

    if (newTask->priority < HIGHTASKPRI) {
        g_statPercpu[0].highTaskSwitch++;
    }

    if (newTask->taskId != idleTaskId) {
        g_statPercpu[0].sumPriority += newTask->priority;
        g_statPercpu[0].prioritySwitch++;
    }
}

LITE_OS_SEC_TEXT_MINOR VOID OsSchedStat(LosTaskCB *runTask, LosTaskCB *newTask)
{
    if (g_statisticsStartFlag != TRUE) {
        return;
    }

    OsSchedStatPerCpu(runTask, newTask, LOS_CurrNanosec());
}

LITE_OS_SEC_TEXT_MINOR VOID OsSchedStatHwi(HWI_HANDLE_T hwiNum)
{
    if ((g_statisticsStartFlag != TRUE) || (hwiNum == OS_TICK_INT_NUM)) {
        return;
    }

    g_statPercpu[0].hwiNum++;
}

LITE_OS_SEC_TEXT_MINOR VOID OsSchedStatPause(VOID)
{
    UINT64 runtime;
    LosTaskCB *taskNow = NULL;
    UINT64 now = LOS_CurrNanosec();

    if (g_statisticsStartFlag != TRUE) {
        return;
    }

    taskNow = OsCurrTaskGet();

    if (taskNow->taskId == OsGetIdleTaskId()) {
        runtime = now - g_statPercpu[0].idleStarttime;
        g_statPercpu[0].idleRuntime += runtime;
    }

    if (taskNow->priority < HIGHTASKPRI) {
        runtime = now - g_statPercpu[0].highTaskStarttime;
        g_statPercpu[0].highTaskRuntime += runtime;
    }
}

LITE_OS_SEC_TEXT_MINOR VOID OsSchedStatResume(VOID)
{
    LosTaskCB *taskNow = NULL;
    UINT64 now = LOS_CurrNanosec();

    if (g_statisticsStartFlag != TRUE) {
        return;
    }

    taskNow = OsCurrTaskGet();

    if (taskNow->taskId == OsGetIdleTaskId()) {
        g_statPercpu[0].idleStarttime = now;
    }

    if (taskNow->priority < HIGHTASKPRI) {
        g_statPercpu[0].highTaskStarttime = now;
    }
}

LITE_OS_SEC_TEXT_MINOR UINT32 OsSchedStatCpuInfoGet(SchedStatCpuInfo *info, UINT32 cpuId)
{
    if ((info == NULL) || (cpuId >= LOSCFG_KERNEL_CORE_NUM)) {
        return LOS_NOK;
    }

    info->pastTime = g_statisticsPastTime;
    info->taskSwiNum = g_statPercpu[cpuId].contextSwitch;
    info->hwiNum = g_statPercpu[cpuId].hwiNum;
    info->idleTaskTimePercent = (g_statisticsPastTime == 0) ? 0 :
        (UINT32)((g_statPercpu[cpuId].idleRuntime * DECIMAL_TO_PERCENTAGE * 100) / g_statisticsPastTime);
    info->noIdleTaskSwiAvgPri = (g_statPercpu[cpuId].prioritySwitch == 0) ? 0 :
        (UINT32)((g_statPercpu[cpuId].sumPriority * 100) / g_statPercpu[cpuId].prioritySwitch);
    info->hiTaskTimePercent = (g_statisticsPastTime == 0) ? 0 :
        (UINT32)((g_statPercpu[cpuId].highTaskRuntime * DECIMAL_TO_PERCENTAGE * 100) / g_statisticsPastTime);
    info->hiTaskSwiNum = g_statPercpu[cpuId].highTaskSwitch;
    info->hiTaskAvgRunTime = (g_statPercpu[cpuId].highTaskSwitch == 0) ? 0 :
        (UINT32)(g_statPercpu[cpuId].highTaskRuntime / g_statPercpu[cpuId].highTaskSwitch / 1000);
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR VOID OsSchedStatShowNamePrint(VOID)
{
    PRINTK("CPU       Idle(%%)    ContextSwitch      HwiNum      "
           "Avg Pri      HiTask(%%)        HiTask SwiNum     HiTask P(ms)\n");
    PRINTK("----    ---------      -----------    --------    ---------     "
           "----------         ------------       ----------\n");
}

LITE_OS_SEC_TEXT_MINOR VOID OsSchedStatShow(VOID)
{
    UINT32 cpuId, ret;
    SchedStatCpuInfo info;

    PRINTK("\nPassed Time: %u.%03u ms\n", (UINT32)(g_statisticsPastTime / NS_PER_MS),
           (UINT32)((g_statisticsPastTime % NS_PER_MS) / 1000));
    PRINTK("--------------------------------\n");
    OsSchedStatShowNamePrint();

    for (cpuId = 0; cpuId < LOSCFG_KERNEL_CORE_NUM; cpuId++) {
        (VOID)memset(&info, 0, sizeof(info));
        ret = OsSchedStatCpuInfoGet(&info, cpuId);
        if (ret != LOS_OK) {
            continue;
        }
        PRINTK("CPU%u   %6u.%02u%17u%12u   %6u.%02u    %6u.%02u%21u    %6u.%03u\n",
               cpuId,
               info.idleTaskTimePercent / 100, info.idleTaskTimePercent % 100,
               info.taskSwiNum, info.hwiNum,
               info.noIdleTaskSwiAvgPri / 100, info.noIdleTaskSwiAvgPri % 100,
               info.hiTaskTimePercent / 100, info.hiTaskTimePercent % 100,
               info.hiTaskSwiNum,
               info.hiTaskAvgRunTime / 1000, info.hiTaskAvgRunTime % 1000);
    }
}

LITE_OS_SEC_TEXT_MINOR VOID OsSchedStatStart(VOID)
{
    LosTaskCB *taskCB = NULL;
    UINT32 intSave;

    SCHEDULER_LOCK(intSave);

    if (g_statisticsStartFlag) {
        SCHEDULER_UNLOCK(intSave);
        PRINTK("schedule statistic has started\n");
        return;
    }

    (VOID)memset(g_statPercpu, 0, sizeof(g_statPercpu));
    g_statisticsStartTime = LOS_CurrNanosec();

    taskCB = OsCurrTaskGet();
    if (taskCB->taskId == OsGetIdleTaskId()) {
        g_statPercpu[0].idleStarttime = g_statisticsStartTime;
    } else {
        g_statPercpu[0].sumPriority += taskCB->priority;
        g_statPercpu[0].prioritySwitch++;
    }

    if (taskCB->priority < HIGHTASKPRI) {
        g_statPercpu[0].highTaskStarttime = g_statisticsStartTime;
        g_statPercpu[0].highTaskSwitch++;
    }
    g_statisticsStartFlag = TRUE;

    SCHEDULER_UNLOCK(intSave);
    PRINTK("schedule statistic start\n");
}

LITE_OS_SEC_TEXT_MINOR VOID OsSchedStatStop(VOID)
{
    LosTaskCB *taskCB = NULL;
    UINT64 statisticsStopTime;
    UINT64 runtime;
    UINT32 intSave;

    SCHEDULER_LOCK(intSave);

    if (g_statisticsStartFlag != TRUE) {
        SCHEDULER_UNLOCK(intSave);
        PRINTK("Please set schedule statistic start\n");
        return;
    }

    g_statisticsStartFlag = FALSE;
    statisticsStopTime = LOS_CurrNanosec();
    g_statisticsPastTime = statisticsStopTime - g_statisticsStartTime;
    g_statisticsStartTime = 0;

    taskCB = OsCurrTaskGet();
    if (taskCB->taskId == OsGetIdleTaskId()) {
        runtime = statisticsStopTime - g_statPercpu[0].idleStarttime;
        g_statPercpu[0].idleRuntime += runtime;
        g_statPercpu[0].idleStarttime = 0;
    }

    if (taskCB->priority < HIGHTASKPRI) {
        runtime = statisticsStopTime - g_statPercpu[0].highTaskStarttime;
        g_statPercpu[0].highTaskRuntime += runtime;
        g_statPercpu[0].highTaskStarttime = 0;
    }

    SCHEDULER_UNLOCK(intSave);
    OsSchedStatShow();
}

#ifdef LOSCFG_SHELL
LITE_OS_SEC_TEXT_MINOR UINT32 OsShellCmdSchedStatStart(UINT32 argc, const CHAR **argv)
{
    (VOID)argc;
    (VOID)argv;
    OsSchedStatStart();
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 OsShellCmdSchedStatStop(UINT32 argc, const CHAR **argv)
{
    (VOID)argc;
    (VOID)argv;
    OsSchedStatStop();
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 OsShellCmdSchedStatInfo(UINT32 argc, const CHAR **argv)
{
    (VOID)argc;
    (VOID)argv;
    OsSchedStatShow();
    return LOS_OK;
}
#endif /* LOSCFG_SHELL */
#endif /* LOSCFG_DEBUG_SCHED_STATISTICS */
