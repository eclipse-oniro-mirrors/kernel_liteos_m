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

#include "los_task_pri.h"
#include "los_box_pri.h"
#include "arch/mpu.h"
#include "arch/task.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef LOSCFG_OBSOLETE_API
#define FIRST_ARG    (taskCB->args[0])
#define SECOND_ARG   (taskCB->args[1])
#define THIRD_ARG    (taskCB->args[2])
#define FOURTH_ARG   (taskCB->args[3])
#endif

VOID *g_runTask = NULL;
VOID *g_oldTask = NULL;

LITE_OS_SEC_TEXT_MINOR VOID OsTaskExit(VOID)
{
    (VOID)LOS_IntLock();
    while (1) {}
}

LITE_OS_SEC_TEXT_INIT VOID *OsTaskStackInit(UINT32 taskId, UINT32 stackSize, VOID *topStack)
{
    TaskContext *taskContext = NULL;

    OsStackInit(topStack, stackSize);
    taskContext = (TaskContext *)(((UINTPTR)topStack + stackSize) - sizeof(TaskContext));

#ifdef LOSCFG_ARCH_FPU_ENABLE
    taskContext->excReturn = 0xFFFFFFFDU;
#endif

    taskContext->uwR4 = 0x04040404U;
    taskContext->uwR5 = 0x05050505U;
    taskContext->uwR6 = 0x06060606U;
    taskContext->uwR7 = 0x07070707U;
    taskContext->uwR8 = 0x08080808U;
    taskContext->uwR9 = 0x09090909U;
    taskContext->uwR10 = 0x10101010U;
    taskContext->uwR11 = 0x11111111U;
    taskContext->uwPriMask = 0U;
    taskContext->uwR0 = taskId;
    taskContext->uwR1 = 0x01010101U;
    taskContext->uwR2 = 0x02020202U;
    taskContext->uwR3 = 0x03030303U;
    taskContext->uwR12 = 0x12121212U;

    taskContext->uwLR = (UINT32)(UINTPTR)OsTaskExit;
    taskContext->uwPC = (UINT32)(UINTPTR)OsTaskEntry;
    taskContext->uwxPSR = 0x01000000U;

    return (VOID *)taskContext;
}

LITE_OS_SEC_TEXT STATIC VOID OsUserTaskEntry(UINT32 taskID)
{
    const LosTaskCB *taskCB = OS_TCB_FROM_TID(taskID);
    TaskContext *userSP = (TaskContext *)taskCB->userStackPointer;

    userSp->uwPC = (UINT32)(UINTPTR)taskCB->taskEntry;
#ifdef LOSCFG_OBSOLETE_API
    OsUserspaceEnter(FIRST_ARG, SECOND_ARG, THIRD_ARG, FOURTH_ARG);
#else
    OsUserspaceEnter(taskCB->args);
#endif
}

LITE_OS_SEC_TEXT STATIC VOID *OsTskUserStackInit(VOID **stackPtr, VOID *userSP, UINT32 userStackSize,
                                                const LosBoxCB *boxCB, UINT32 taskId)
{
    (VOID)taskId;
    TaskContext *taskContext = (TaskContext*)*stackPtr;
    taskContext->uwPC = (UINT32)(UINTPTR)OsUserTaskEntry;  
#ifdef LOSCFG_DYNAMIC_BOX
    if (boxCB->dynboxWord != 0) {
        taskContext->uwR9 = boxCB->dynboxWord; /* Word used as got address */
    }
#endif
    OsStackInit(userSP, userStackSize);

    taskContext = (TaskContext *)(((UINTPTR)userSP + userStackSize) - sizeof(TaskContext));

#ifdef LOSCFG_ARCH_FPU_ENABLE
    taskContext->excReturn = 0xFFFFFFFDU;
#endif

    taskContext->uwR4 = 0x04040404U;
    taskContext->uwR5 = 0x05050505U;
    taskContext->uwR6 = 0x06060606U;
    taskContext->uwR7 = 0x07070707U;
    taskContext->uwR8 = 0x08080808U;
    taskContext->uwR9 = 0x09090909U;
    taskContext->uwR10 = 0x10101010U;
    taskContext->uwR11 = 0x11111111U;
    taskContext->uwPriMask = 0U;
    taskContext->uwR0 = taskId;
    taskContext->uwR1 = 0x01010101U;
    taskContext->uwR2 = 0x02020202U;
    taskContext->uwR3 = 0x03030303U;
    taskContext->uwR12 = 0x12121212U;

    /* bit0 must be set: ArchStartToRun starts the first task with a direct
     * "bx" to uwPC; an even target raises INVSTATE (HardFault). */
    taskContext->uwLR = (UINT32)(UINTPTR)OsUsrTaskExit;
    taskContext->uwPC = (UINT32)(UINTPTR)NULL;
    taskContext->uwxPSR = 0x01000000U;

    return (VOID*)taskContext;
}

VOID OsSetQueuePerm(VOID)
{
#ifdef LOSCFG_ARCH_CORTEX_M55
    OsEnableAllBoxPerm();
#endif
}

VOID OsResetQueuePerm(VOID)
{
#ifdef LOSCFG_ARCH_CORTEX_M55
    OsEnableCurBoxPerm();
#endif
}

#ifdef __cplusplus
}
#endif /* __cplusplus */