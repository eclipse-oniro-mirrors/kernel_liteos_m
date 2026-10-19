/*
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

#include "los_typedef.h"
#include "los_arch_interrupt.h"
#include "los_arch_context.h"
#include "los_task_pri.h"
#include "los_memory.h"
#include "los_exc.h"
#include "los_debug.h"

ExcInfo g_excInfo = {0};
UINT32 g_percpuNestCount[LOSCFG_KERNEL_CORE_NUM] = {0};

STATIC VOID OsExcInfoDisplay(const ExcInfo *excInfo)
{
#if (LOSCFG_KERNEL_PRINTF != 0)
    CHAR *phaseStr[] = {"exc in init", "exc in task", "exc in hwi"};

    PRINTK("*************Exception Information**************\n");
    PRINTK("Type      = %d\n", excInfo->type);
    PRINTK("ThrdPid   = %d\n", excInfo->thrdPid);
    PRINTK("Phase     = %s\n", phaseStr[excInfo->phase]);
    PRINTK("FaultAddr = 0x%x\n", excInfo->faultAddr);
    PRINTK("nestCnt   = %d\n", excInfo->nestCnt);
    if (excInfo->context != NULL) {
        PRINTK("LR        = 0x%x\n", excInfo->context->uwLR);
        PRINTK("PC        = 0x%x\n", excInfo->context->uwPC);
    }
    if (excInfo->phase == OS_EXC_IN_TASK) {
        LosTaskCB *taskCB = OsCurrTaskGet();
        PRINTK("Task name = %s\n", taskCB->taskName);
        PRINTK("Task ID   = %d\n", taskCB->taskId);
    }
    UINT32 ret = LOS_MemIntegrityCheck(LOSCFG_SYS_HEAP_ADDR);
    if (ret == LOS_OK) {
        PRINTK("system heap memcheck passed!\n");
    }
#endif
}

LITE_OS_SEC_TEXT_INIT VOID HalExcHandleEntry(UINT32 excType, UINT32 faultAddr, UINT32 pid, EXC_CONTEXT_S *excBufAddr)
{
    UINT16 tmpFlag = (UINT16)((excType >> 16) & OS_NULL_SHORT);

    g_percpuNestCount[ArchCurrCpuid()]++;
    g_excInfo.nestCnt++;

    g_excInfo.type = (UINT16)(excType & OS_NULL_SHORT);

    if (tmpFlag & OS_EXC_FLAG_FAULTADDR_VALID)
    {
        g_excInfo.faultAddr = faultAddr;
    }
    else
    {
        g_excInfo.faultAddr = OS_EXC_IMPRECISE_ACCESS_ADDR;
    }
    LosTaskCB *runTask = OsCurrTaskGet();
    if (runTask != NULL)
    {
        if (tmpFlag & OS_EXC_FLAG_IN_HWI)
        {
            g_excInfo.phase = OS_EXC_IN_HWI;
            g_excInfo.thrdPid = pid;
        }
        else
        {
            g_excInfo.phase = OS_EXC_IN_TASK;
            g_excInfo.thrdPid = runTask->taskId;
        }
    }
    else
    {
        g_excInfo.phase = OS_EXC_IN_INIT;
        g_excInfo.thrdPid = OS_NULL_INT;
    }
    g_excInfo.context = excBufAddr;

    if (g_excRegHook != NULL)
    {
        g_excRegHook(excType, g_excInfo.context);
    }
    else
    {
        OsDoExcHook(EXC_INTERRUPT);
        OsExcInfoDisplay(&g_excInfo);
    }
    ArchTaskExit();
}
