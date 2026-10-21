/*
 * Copyright (c) 2024 morpheus
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

/* ****************************************************************************
 Function    : ArchInit
 Description : arch init function
 **************************************************************************** */
LITE_OS_SEC_TEXT_INIT VOID ArchInit(VOID)
{
    HalHwiInit();
}

/* ****************************************************************************
 Function    : ArchTaskExit
 Description : Task exit function
 **************************************************************************** */
LITE_OS_SEC_TEXT_MINOR NORETURN VOID ArchTaskExit(VOID)
{
    (VOID)LOS_IntLock();
    while (1) {
    }
}

/* ****************************************************************************
 Function    : ArchTaskStackInit
 Description : Task stack initialization function
 Input       : taskId     --- TaskID
               stackSize  --- Total size of the stack
               topStack   --- Top of task's stack
 Return      : Context pointer
 **************************************************************************** */
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
    /* 0xAAA0000000000000LL : float reg initialed magic word */
    for (index = 0; index < FP_REGS_NUM; index++) {
        context->D[index] = 0xAAA0000000000000LL + index; /* D0 - D31 */
    }
    context->regFPSCR = 0;
    context->regFPEXC = (1U << 30); /* FP_EN */
#endif

    return (VOID *)context;
}

/* ****************************************************************************
 Function    : ArchSignalContextInit
 Description : Initialize a signal handler context on the task stack
 **************************************************************************** */
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
