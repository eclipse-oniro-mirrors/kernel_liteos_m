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

#include "los_context.h"
#include "securec.h"
#include "los_arch_context.h"
#include "los_arch_interrupt.h"
#include "los_task.h"
#include "los_sched.h"
#include "los_interrupt.h"
#include "los_typedef.h"

extern VOID OsTaskEntry(UINT32 taskId);

LITE_OS_SEC_TEXT_INIT VOID ArchInit(VOID)
{
    HalHwiInit();
}

LITE_OS_SEC_TEXT_MINOR NORETURN VOID ArchTaskExit(VOID)
{
    (VOID)LOS_IntLock();
    while (1) {
    }
}

LITE_OS_SEC_TEXT_INIT VOID *ArchTaskStackInit(UINT32 taskId, UINT32 stackSize, VOID *topStack)
{
    UINT32 index = 1;
    TaskContext *context = (TaskContext *)((UINTPTR)topStack + stackSize - sizeof(TaskContext));

    (VOID)memset_s((VOID *)context, sizeof(TaskContext), 0, sizeof(TaskContext));

    context->R[0] = taskId;
    context->R[index++] = 0x01010101L; /* R1, 0x01010101 : reg initialed magic word */
    for (; index < GEN_REGS_NUM; index++) {
        context->R[index] = context->R[index - 1] + context->R[1]; /* R2 - R12 */
    }
    context->LR = (UINT32)(UINTPTR)ArchTaskExit;
    context->PC = (UINT32)(UINTPTR)OsTaskEntry;

    context->regPSR = PSR_MODE_SVC_ARM;
#ifdef LOSCFG_ARCH_FPU_ENABLE
    for (index = 0; index < FP_REGS_NUM; index++) {
        context->D[index] = 0xAAA0000000000000LL + index; /* D0 - D31 */
    }
    context->regFPSCR = 0;
    context->regFPEXC = (1U << 30); /* FP_EN */
#endif

    return (VOID *)context;
}

VOID *ArchSignalContextInit(VOID *stackPointer, VOID *stackTop, UINTPTR sigHandler, UINT32 param)
{
    UNUSED(stackTop);
    TaskContext *context = (TaskContext *)((UINTPTR)stackPointer - sizeof(TaskContext));
    (VOID)memset_s((VOID *)context, sizeof(TaskContext), 0, sizeof(TaskContext));

    context->PC = (UINT32)sigHandler;
    context->regPSR = PSR_MODE_SVC_ARM;
    context->LR = 0;
    context->R[0] = param;

    return (VOID *)context;
}
