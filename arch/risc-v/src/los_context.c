/*
 * Copyright (c) 2013-2020, Huawei Technologies Co., Ltd. All rights reserved.
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

#include "los_arch_context.h"
#include "los_arch_interrupt.h"
#include "los_arch_timer.h"
#include "los_task.h"
#include "los_task_pri.h"
#include "los_sched.h"
#include "los_sched_pri.h"
#include "los_memory.h"
#include "soc_common.h"

LITE_OS_SEC_BSS LosTaskCB *g_newTask = NULL;

LITE_OS_SEC_TEXT_INIT VOID ArchInit(VOID)
{
    HalHwiInit();
}

LITE_OS_SEC_TEXT_MINOR VOID ArchTaskExit(VOID)
{
    ArchIntLock();
    while (1) {
    }
}

LITE_OS_SEC_TEXT_INIT VOID *ArchTaskStackInit(UINT32 taskId, UINT32 stackSize, VOID *topStack)
{
    UINT32 index;
    TaskContext *context = NULL;

    /* los_task.c already memset the stack + stamped OS_TASK_MAGIC_WORD, so
     * do NOT re-memset here. */
    context = (TaskContext *)(((UINTPTR)topStack + stackSize) - sizeof(TaskContext));

    /* Single loop over the contiguous s11..ra region. */
    ((UINT32 *)context)[S11_REG_INDEX] = S11_INIT_VALUE;
    for (index = S11_REG_INDEX + 1; index < (sizeof(TaskContext) / sizeof(UINT32)); index++) {
        ((UINT32 *)context)[index] = ((UINT32 *)context)[index - 1] + STACK_INIT_ACC;
    }
#ifndef LOSCFG_ARCH_FPU_DISABLE
    context->fcsr = 0x0;  /* fflags/frm start clean */
#endif
    context->mstatus = LOSCFG_MSTATUS_M;
    context->mepc = (UINT32)(UINTPTR)OsTaskEntry;
    context->a0 = taskId;
    context->ra = (UINT32)(UINTPTR)ArchTaskExit;

    return (VOID *)context;
}


