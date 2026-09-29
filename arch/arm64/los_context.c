/*
 * Copyright (c) 2026 morpheus
 * AArch64 task context init. Ported from hs-fbb arch_lite/arm64/src/task.c,
 * adapted to the liteos_m contract (see cortex_a_r/context.c).
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
 * Function    : ArchInit
 * Description : arch init function (called from OsArchInit, SYS_INIT ARCH)
 * **************************************************************************** */
LITE_OS_SEC_TEXT_INIT VOID ArchInit(VOID)
{
    HalHwiInit();
}

/* ****************************************************************************
 * Function    : ArchTaskExit
 * Description : Task exit function
 * **************************************************************************** */
LITE_OS_SEC_TEXT_MINOR NORETURN VOID ArchTaskExit(VOID)
{
    (VOID)LOS_IntLock();
    while (1) {
        __asm__ volatile("wfe");
    }
}

/* ****************************************************************************
 * Function    : ArchTaskStackInit
 * Description : Task stack initialization function
 * Input       : taskId     --- TaskID
 *               stackSize  --- Total size of the stack
 *               topStack   --- Top of task's stack
 * Return      : Context pointer
 * **************************************************************************** */
LITE_OS_SEC_TEXT_INIT VOID *ArchTaskStackInit(UINT32 taskId, UINT32 stackSize, VOID *topStack)
{
    UINT32 index;
    TaskContext *taskContext = NULL;

    /* poison the stack (0xCA... word pattern), then place the context at its top */
    (VOID)memset_s(topStack, stackSize, 0xCA, stackSize);
    taskContext = (TaskContext *)(((UINTPTR)topStack + stackSize) - sizeof(TaskContext));

    /* initialize the task context */
    taskContext->NZCV = 0;
    taskContext->PC = (UINTPTR)OsTaskEntry;
    taskContext->LR = (UINTPTR)ArchTaskExit;
    taskContext->X[0] = taskId;                /* X0 */

    /* 0xAAA0000000000001LL : reg initialed magic word */
    for (index = 1; index < GEN_REGS_NUM; index++) {
        taskContext->X[index] = 0xAAA0000000000001ULL + index; /* X1 - X29 */
    }

    taskContext->regSPSR = 0x5;                /* EL1h, IRQ/FIQ enabled */

#ifdef LOSCFG_ARCH_FPU_ENABLE
    /* 0xAAA0000000000001LL : float reg initialed magic word */
    for (index = 0; index < FP_REGS_NUM; index++) {
        taskContext->Q[index] = (UINT128)(0xAAA0000000000001ULL + index); /* Q0-Q31 */
    }

    taskContext->regFPCR = 0;
    taskContext->regFPSR = 0;
#endif

    return (VOID *)taskContext;
}

/* ****************************************************************************
 * Function    : ArchSignalContextInit
 * Description : Initialize a signal handler context on the task stack
 * **************************************************************************** */
VOID *ArchSignalContextInit(VOID *stackPointer, VOID *stackTop, UINTPTR sigHandler, UINT32 param)
{
    (VOID)stackTop;
    TaskContext *context = (TaskContext *)((UINTPTR)stackPointer - sizeof(TaskContext));
    (VOID)memset_s((VOID *)context, sizeof(TaskContext), 0, sizeof(TaskContext));

    context->PC = sigHandler;
    context->regSPSR = 0x3c5; /* EL1h, DAIF masked */
    context->LR = 0;
    context->X[0] = param;

    return (VOID *)context;
}
