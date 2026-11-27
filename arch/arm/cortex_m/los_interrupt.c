/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Cortex-M4 exception handling (gcc variant).
 * The NVIC interrupt controller driver moved to the driver layer:
 * drivers/interrupt/arm_nvic.c (HwiControllerOps, HalInterrupt,
 * HalHwiInit). This file keeps only the exception dump/display logic
 * and HalExcHandleEntry. Filename is kept because los_exc.S already
 * occupies the los_exc.o object name in this directory.
 */

#include "securec.h"
#include "los_context.h"
#include "los_arch_interrupt.h"
#include "los_task_pri.h"
#include "los_memory.h"
#include "los_membox.h"
#ifdef LOSCFG_SHELL_EXCINFO_DUMP
#include "los_exc_pri.h"
#endif
#include "los_hwi_pri.h"
#include "los_arch.h"
#ifdef LOSCFG_KERNEL_HPL
#include "los_hpl_pri.h"
#endif

#ifdef LOSCFG_BOX
#include "los_box_pri.h"
#include "los_sched_pri.h"

UINT32 g_boxFaultMsp;
UINT32 g_boxFaultReturn;
extern VOID ArchBoxFaultSwitch(VOID) __attribute__((noreturn));
#endif

#define ARM_EXCEPTION_SYSTICK      15

/* ArchIntLocked: read PRIMASK to report whether interrupts are masked.
 * The NVIC interrupt controller driver (HwiControllerOps, HalInterrupt,
 * HalHwiInit) moved to drivers/interrupt/arm_nvic.c.
 */
UINT32 ArchIntLocked(VOID)
{
    return __get_PRIMASK() & 0x1;
}

#define FAULT_STATUS_REG_BIT            32

ExcInfo g_excInfo = {0};

UINT8 g_uwExcTbl[FAULT_STATUS_REG_BIT] = {
    0, 0, 0, 0, 0, 0, OS_EXC_UF_DIVBYZERO, OS_EXC_UF_UNALIGNED,
    0, 0, 0, 0, OS_EXC_UF_NOCP, OS_EXC_UF_INVPC, OS_EXC_UF_INVSTATE, OS_EXC_UF_UNDEFINSTR,
    0, 0, 0, OS_EXC_BF_STKERR, OS_EXC_BF_UNSTKERR, OS_EXC_BF_IMPRECISERR, OS_EXC_BF_PRECISERR, OS_EXC_BF_IBUSERR,
    0, 0, 0, OS_EXC_MF_MSTKERR, OS_EXC_MF_MUNSTKERR, 0, OS_EXC_MF_DACCVIOL, OS_EXC_MF_IACCVIOL
};

#if (LOSCFG_KERNEL_PRINTF != 0)
#ifndef LOSCFG_EXC_SIMPLE_INFO
STATIC VOID OsExcNvicDump(VOID)
{
#define OS_NR_NVIC_EXC_DUMP_TYPES   7
    UINT32 *base = NULL;
    UINT32 len, i, j;
    UINT32 rgNvicBases[OS_NR_NVIC_EXC_DUMP_TYPES] = {
        OS_NVIC_SETENA_BASE, OS_NVIC_SETPEND_BASE, OS_NVIC_INT_ACT_BASE,
        OS_NVIC_PRI_BASE, OS_NVIC_EXCPRI_BASE, OS_NVIC_SHCSR, OS_NVIC_INT_CTRL
    };
    UINT32 rgNvicLens[OS_NR_NVIC_EXC_DUMP_TYPES] = {
        OS_NVIC_INT_ENABLE_SIZE, OS_NVIC_INT_PEND_SIZE, OS_NVIC_INT_ACT_SIZE,
        OS_NVIC_INT_PRI_SIZE, OS_NVIC_EXCPRI_SIZE, OS_NVIC_SHCSR_SIZE,
        OS_NVIC_INT_CTRL_SIZE
    };
    CHAR strRgEnable[] = "enable";
    CHAR strRgPending[] = "pending";
    CHAR strRgActive[] = "active";
    CHAR strRgPriority[] = "priority";
    CHAR strRgException[] = "exception";
    CHAR strRgShcsr[] = "shcsr";
    CHAR strRgIntCtrl[] = "control";
    CHAR *strRgs[] = {
        strRgEnable, strRgPending, strRgActive, strRgPriority,
        strRgException, strRgShcsr, strRgIntCtrl
    };

    PRINTK("\r\nOS exception NVIC dump:\n");
    for (i = 0; i < OS_NR_NVIC_EXC_DUMP_TYPES; i++) {
        base = (UINT32 *)rgNvicBases[i];
        len = rgNvicLens[i];
        PRINTK("interrupt %s register, base address: %p, size: 0x%x\n", strRgs[i], base, len);
        len = (len >> 2); /* 2: Gets the next register offset */
        for (j = 0; j < len; j++) {
            PRINTK("0x%x ", *(base + j));
            if ((j != 0) && ((j % 16) == 0)) { /* 16: print wrap line */
                PRINTK("\n");
            }
        }
        PRINTK("\n");
    }
}
#endif

STATIC VOID OsExcTypeInfo(const ExcInfo *excInfo)
{
    CHAR *phaseStr[] = {"exc in init", "exc in task", "exc in hwi"};

    PRINTK("Type      = %d\n", excInfo->type);
    PRINTK("ThrdPid   = %d\n", excInfo->thrdPid);
    PRINTK("Phase     = %s\n", phaseStr[excInfo->phase]);
    PRINTK("FaultAddr = 0x%x\n", excInfo->faultAddr);
}

STATIC VOID OsExcCurTaskInfo(const ExcInfo *excInfo)
{
    PRINTK("Current task info:\n");
    if (excInfo->phase == OS_EXC_IN_TASK) {
        LosTaskCB *taskCB = OS_TCB_FROM_TID(LOS_CurTaskIDGet());
        PRINTK("Task name = %s\n", taskCB->taskName);
        PRINTK("Task ID   = %d\n", taskCB->taskId);
        PRINTK("Task SP   = %p\n", taskCB->stackPointer);
        PRINTK("Task ST   = 0x%x\n", taskCB->topOfStack);
        PRINTK("Task SS   = 0x%x\n", taskCB->stackSize);
    } else if (excInfo->phase == OS_EXC_IN_HWI) {
        PRINTK("Exception occur in interrupt phase!\n");
    } else {
        PRINTK("Exception occur in system init phase!\n");
    }
}

STATIC VOID OsExcRegInfo(const ExcInfo *excInfo)
{
    PRINTK("Exception reg dump:\n");
    PRINTK("PC        = 0x%x\n", excInfo->context->uwPC);
    PRINTK("LR        = 0x%x\n", excInfo->context->uwLR);
    PRINTK("SP        = 0x%x\n", excInfo->context->uwSP);
    PRINTK("R0        = 0x%x\n", excInfo->context->uwR0);
    PRINTK("R1        = 0x%x\n", excInfo->context->uwR1);
    PRINTK("R2        = 0x%x\n", excInfo->context->uwR2);
    PRINTK("R3        = 0x%x\n", excInfo->context->uwR3);
    PRINTK("R4        = 0x%x\n", excInfo->context->uwR4);
    PRINTK("R5        = 0x%x\n", excInfo->context->uwR5);
    PRINTK("R6        = 0x%x\n", excInfo->context->uwR6);
    PRINTK("R7        = 0x%x\n", excInfo->context->uwR7);
    PRINTK("R8        = 0x%x\n", excInfo->context->uwR8);
    PRINTK("R9        = 0x%x\n", excInfo->context->uwR9);
    PRINTK("R10       = 0x%x\n", excInfo->context->uwR10);
    PRINTK("R11       = 0x%x\n", excInfo->context->uwR11);
    PRINTK("R12       = 0x%x\n", excInfo->context->uwR12);
    PRINTK("PriMask   = 0x%x\n", excInfo->context->uwPriMask);
    PRINTK("xPSR      = 0x%x\n", excInfo->context->uwxPSR);
}

#if (LOSCFG_KERNEL_BACKTRACE == 1)
STATIC VOID OsExcBackTraceInfo(const ExcInfo *excInfo)
{
    UINTPTR LR[LOSCFG_BACKTRACE_DEPTH] = {0};
    UINT32 index;

    OsBackTraceHookCall(LR, LOSCFG_BACKTRACE_DEPTH, 0, excInfo->context->uwSP);

    PRINTK("----- backtrace start -----\n");
    for (index = 0; index < LOSCFG_BACKTRACE_DEPTH; index++) {
        if (LR[index] == 0) {
            break;
        }
        PRINTK("backtrace %d -- lr = 0x%x\n", index, LR[index]);
    }
    PRINTK("----- backtrace end -----\n");
}
#endif

#ifndef LOSCFG_EXC_SIMPLE_INFO
STATIC VOID OsExcMemPoolCheckInfo(VOID)
{
    PRINTK("\r\nmemory pools check:\n");
#if (LOSCFG_PLATFORM_EXC == 1)
    MemInfoCB memExcInfo[OS_SYS_MEM_NUM];
    UINT32 errCnt;
    UINT32 i;

    (VOID)memset_s(memExcInfo, sizeof(memExcInfo), 0, sizeof(memExcInfo));

    errCnt = OsMemExcInfoGet(OS_SYS_MEM_NUM, memExcInfo);
    if (errCnt < OS_SYS_MEM_NUM) {
        errCnt += OsMemboxExcInfoGet(OS_SYS_MEM_NUM - errCnt, memExcInfo + errCnt);
    }

    if (errCnt == 0) {
        PRINTK("all memory pool check passed!\n");
        return;
    }

    for (i = 0; i < errCnt; i++) {
        PRINTK("pool num    = %d\n", i);
        PRINTK("pool type   = %d\n", memExcInfo[i].type);
        PRINTK("pool addr   = 0x%x\n", memExcInfo[i].startAddr);
        PRINTK("pool size   = 0x%x\n", memExcInfo[i].size);
        PRINTK("pool free   = 0x%x\n", memExcInfo[i].free);
        PRINTK("pool blkNum = %d\n", memExcInfo[i].blockSize);
        PRINTK("pool error node addr  = 0x%x\n", memExcInfo[i].errorAddr);
        PRINTK("pool error node len   = 0x%x\n", memExcInfo[i].errorLen);
        PRINTK("pool error node owner = %d\n", memExcInfo[i].errorOwner);
    }
#endif
    UINT32 ret = LOS_MemIntegrityCheck(LOSCFG_SYS_HEAP_ADDR);
    if (ret == LOS_OK) {
        PRINTK("system heap memcheck over, all passed!\n");
    }

    PRINTK("memory pool check end!\n");
}
#endif
#endif

STATIC VOID OsExcInfoDisplay(const ExcInfo *excInfo)
{
#if (LOSCFG_KERNEL_PRINTF != 0)
    PRINTK("*************Exception Information**************\n");
    OsExcTypeInfo(excInfo);
    OsExcCurTaskInfo(excInfo);
    OsExcRegInfo(excInfo);
#if (LOSCFG_KERNEL_BACKTRACE == 1)
    OsExcBackTraceInfo(excInfo);
#endif
#ifndef LOSCFG_EXC_SIMPLE_INFO
    OsGetAllTskInfo();
    OsExcNvicDump();
    OsExcMemPoolCheckInfo();
#endif
#endif
}

#ifdef LOSCFG_BOX
/* Registering an exception hook replaces the default
 * notify+display path in HalExcHandleEntry below, so the box hook needs a
 * way to keep the dump. Thin BOX-only wrapper - with BOX off
 * OsExcInfoDisplay stays STATIC and this file is unchanged.
 *
 * Deviation from OsExcInfoDisplay: the LOSCFG_EXC_SIMPLE_INFO tail
 * (OsGetAllTskInfo / OsExcNvicDump / OsExcMemPoolCheckInfo) is deliberately
 * NOT run here. Under BOX the single COMMON copy of los_printf.c carries the
 * lightweight LOS_PRINTF_COMMON_ENGINE formatter, and running the tail from
 * this exception context escalates to a Lockup after the third task row.
 * Everything a box fault actually needs survives: type/phase/faultAddr,
 * current task, full register dump and the backtrace. */
VOID OsExcInfoDisplayBox(const ExcInfo *excInfo)
{
#if (LOSCFG_KERNEL_PRINTF != 0)
    PRINTK("*************Exception Information**************\n");
    OsExcTypeInfo(excInfo);
    OsExcCurTaskInfo(excInfo);
    OsExcRegInfo(excInfo);
#if (LOSCFG_KERNEL_BACKTRACE == 1)
    OsExcBackTraceInfo(excInfo);
#endif
#endif
}
#endif

LITE_OS_SEC_TEXT_INIT BOOL ArchExcActive(VOID)
{
    UINT32 ipsr = ArchGetIpsr();
    /* 15: SysTick interrupt number and less than this number is exception. */
    return (ipsr < ARM_EXCEPTION_SYSTICK) && (ipsr > 0);
}

/* Pitfall 57: the FLAG_NO_FLOAT branch below bridges two frame geometries
 * with LOS_OFF_SET_OF(EXC_CONTEXT_S, uwR4), so that offset has to match the
 * S16-S31 prefix the struct actually has. los_exc.S picks the geometry at
 * RUNTIME from EXC_RETURN, while the struct picks it at COMPILE time from the
 * FP condition in los_arch_interrupt.h - if those two ever disagree again
 * every exception register reads 64 bytes low, and the dump still looks
 * plausible, which is the worst failure mode. Lock it down.
 *
 * The condition here is written out to be IDENTICAL to the struct's, and both
 * arms are asserted: gating the check on LOSCFG_BOX alone would leave a
 * BOX=off build that enables the FPU unchecked, and would assert the wide
 * size for a BOX=on build whose __FPU_PRESENT is not 1. */
#if ((defined(LOSCFG_BOX) && defined (__FPU_PRESENT) && (__FPU_PRESENT == 1U)) || \
     ((defined (__FPU_PRESENT) && (__FPU_PRESENT == 1U)) && \
      (defined (__FPU_USED   ) && (__FPU_USED    == 1U))     ))
/* Wide: 64 (S16-S31) + 36 (R4-R11 + PRIMASK) + 4 (SP) + 104 (extended
 * hardware frame: 8 words + S0-S15 + FPSCR + reserved). */
_Static_assert((sizeof(EXC_CONTEXT_S) == 208) &&
               (LOS_OFF_SET_OF(EXC_CONTEXT_S, uwR4) == 64),
               "FP-capable build needs the wide EXC_CONTEXT_S: los_exc.S builds "
               "the extended frame whenever CONTROL.FPCA is set (box user code "
               "sets it, which is why LOSCFG_BOX is an arm of the condition)");
#else
/* Short: 36 (R4-R11 + PRIMASK) + 4 (SP) + 32 (basic hardware frame: R0-R3,
 * R12, LR, PC, xPSR). uwR4 is the first member, so its offset is 0 and the
 * FLAG_NO_FLOAT compensation below must be a no-op. */
_Static_assert((sizeof(EXC_CONTEXT_S) == 72) &&
               (LOS_OFF_SET_OF(EXC_CONTEXT_S, uwR4) == 0),
               "non-FP build needs the short EXC_CONTEXT_S (no S16-S31 prefix)");
#endif

#if defined(LOSCFG_BOX) && !defined(LOSCFG_KERNEL_SMP) && !defined(LOSCFG_ARCH_ARM_V6M)
#define OS_IPSR_HARDFAULT      3U
#define OS_IPSR_USAGEFAULT     6U

/* EXC_RETURN of an exception taken from a thread using the PSP, with a basic
 * or an FPU-extended frame. Kernel code runs on the MSP, and so does every
 * exception handler. */
#define OS_EXC_RETURN_THREAD_PSP      0xFFFFFFFDU
#define OS_EXC_RETURN_THREAD_PSP_FP   0xFFFFFFEDU

/* Why a fault raised by a box task was not contained. Nonzero means the
 * system is halted on purpose: the kernel could not prove the fault was a
 * plain user-mode error with the kernel itself between two instructions. */
#define OS_BOX_FAULT_NOT_CPU_FAULT  1U  /* NMI, an interrupt, a debugger or vector-table HardFault */
#define OS_BOX_FAULT_FRAME          2U  /* not taken from a thread using the PSP */
#define OS_BOX_FAULT_NOT_USER       3U  /* the thread was privileged or not in user mode: inside a syscall */
#define OS_BOX_FAULT_FOREIGN_BOX    4U  /* the task is not a task of a live box, or the box is not its own */
#define OS_BOX_FAULT_MID_SWITCH     5U  /* the scheduler does not agree that this task is running */

/* Every guard of OsContainBoxFault() has this shape, so that the function
 * reads as what it is: an ordered list of conditions, where a guard may only
 * dereference what the guards above it have proved valid. */
#define OS_BOX_FAULT_REJECT_IF(cond, why) \
    do {                                  \
        if (cond) {                       \
            reason = (why);               \
            goto OS_BOX_FAULT_REJECTED;   \
        }                                 \
    } while (0)

/* Destroy the box whose user-mode task just faulted, and switch to the task
 * the scheduler picks next, so that the kernel and the other boxes keep
 * running. Called from HalExcHandleEntry() once the fault has been dumped;
 * whatever it rejects falls through to the system halt there.
 *
 * What matters for kernel integrity is not the fault but WHERE it was
 * raised: by an unprivileged thread running its own code, with the kernel
 * between two of its instructions. The kernel state is then exactly what it
 * was before that instruction, and only the box has to go. */
STATIC VOID OsContainBoxFault(VOID)
{
    LosTaskCB *task = OsCurrTaskGet();
    LosBoxCB *box;
    LosTaskCB *member;
    LOS_DL_LIST *node;
    UINT32 ipsr = ArchGetIpsr();
    UINT32 id = g_curBoxId;
    UINT32 intSave;
    UINT32 reason = 0U;

    /* A CPU fault. A HardFault counts only as the escalation of one: the
     * others (debugger, vector table) are not the box's doing. */
    OS_BOX_FAULT_REJECT_IF((ipsr < OS_IPSR_HARDFAULT) || (ipsr > OS_IPSR_USAGEFAULT) ||
                           ((ipsr == OS_IPSR_HARDFAULT) && (SCB->HFSR != SCB_HFSR_FORCED_Msk)),
                           OS_BOX_FAULT_NOT_CPU_FAULT);
    /* Raised by a thread on its own stack: not in a handler, not in kernel code. */
    OS_BOX_FAULT_REJECT_IF((g_boxFaultReturn != OS_EXC_RETURN_THREAD_PSP) &&
                           (g_boxFaultReturn != OS_EXC_RETURN_THREAD_PSP_FP), OS_BOX_FAULT_FRAME);
    /* An unprivileged thread flagged in user mode, so not inside a syscall.
     * CONTROL.nPRIV keeps the thread's privilege while in Handler mode. */
    OS_BOX_FAULT_REJECT_IF((task == NULL) || ((__get_CONTROL() & 1U) == 0U) ||
                           ((task->taskStatus & OS_TASK_STATUS_INUSER) == 0U), OS_BOX_FAULT_NOT_USER);
    /* A task of a live box, and the box mapped for this thread is its own. */
    OS_BOX_FAULT_REJECT_IF((id >= LOSCFG_BASE_BOX_MAX_LIMIT) || (g_boxCBArr == NULL) ||
                           (task->boxCB != &g_boxCBArr[id]) || (task->curBoxId != id) ||
                           !BOX_IS_INIT(g_boxCBArr[id].boxStatus), OS_BOX_FAULT_FOREIGN_BOX);
    /* The scheduler agrees this task is the running one: the kernel is not
     * between two steps of a task switch. */
    OS_BOX_FAULT_REJECT_IF((task != g_oldTask) || (task != g_runTask), OS_BOX_FAULT_MID_SWITCH);

    /* MSP and the exception masks protect the transaction. Remove only the
     * doomed box from eligibility; leave the old stack valid for switch hooks. */
    box = &g_boxCBArr[id];
    g_sysMode = 0U;
    PRINT_ERR("BOX_EXC: destroying box %u (fault in its task): type=%u addr=0x%x\n",
              id, (UINT32)g_excInfo.type, (UINT32)g_excInfo.faultAddr);
    SCHEDULER_LOCK(intSave);
    LOS_DL_LIST_FOR_EACH(node, &box->taskCBList) {
        member = LOS_DL_LIST_ENTRY(node, LosTaskCB, boxTaskList);
        OsSchedTaskDeQueue(member);
    }
    OsSchedResched();
    SCHEDULER_UNLOCK(intSave);
    if ((g_runTask == task) || (g_runTask->boxCB == box)) {
        ArchTaskExit();     /* needs a scheduler bug; never resume the doomed box */
    }
    OsBoxDestroy((UINT16)id);
    /* The fault is handled: drop its sticky status bits, so that they neither
     * fire again once the vector is re-enabled nor blur the next diagnosis. */
    SCB->CFSR = 0xFFFFFFFFU;
    if (ipsr == OS_IPSR_HARDFAULT) {
        SCB->HFSR = SCB_HFSR_FORCED_Msk;
        SCB->SHCSR &= ~(SCB_SHCSR_USGFAULTPENDED_Msk | SCB_SHCSR_BUSFAULTPENDED_Msk |
                        SCB_SHCSR_MEMFAULTPENDED_Msk);
    }
    SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk;    /* PendSV is superseded by this switch */
    __DSB();
    __ISB();
    g_excInfo.context = NULL;
    g_excInfo.nestCnt--;
    g_intCount[ArchCurrCpuid()]--;
    ArchBoxFaultSwitch();

OS_BOX_FAULT_REJECTED:
    /* A first-level fault: the dump above already ran on this stack. */
    if ((g_intCount[ArchCurrCpuid()] == 1U) && (g_excInfo.nestCnt == 1U)) {
        PRINT_ERR("BOX_EXC: box %u fault NOT contained, reason=%u ipsr=%u cfsr=0x%x hfsr=0x%x ctrl=0x%x ret=0x%x\n",
                  id, reason, ipsr, SCB->CFSR, SCB->HFSR, __get_CONTROL(), g_boxFaultReturn);
    }
    return;
}
#undef OS_BOX_FAULT_REJECT_IF
#endif
LITE_OS_SEC_TEXT_INIT VOID HalExcHandleEntry(UINT32 excType, UINT32 faultAddr, UINT32 pid, EXC_CONTEXT_S *excBufAddr)
{
    UINT16 tmpFlag = (excType >> 16) & OS_NULL_SHORT; /* 16: Get Exception Type */
    g_intCount[ArchCurrCpuid()]++;
    g_excInfo.nestCnt++;

    g_excInfo.type = excType & OS_NULL_SHORT;

    if (tmpFlag & OS_EXC_FLAG_FAULTADDR_VALID) {
        g_excInfo.faultAddr = faultAddr;
    } else {
        g_excInfo.faultAddr = OS_EXC_IMPRECISE_ACCESS_ADDR;
    }
    if (g_runTask != NULL) {
        if (tmpFlag & OS_EXC_FLAG_IN_HWI) {
            g_excInfo.phase = OS_EXC_IN_HWI;
            g_excInfo.thrdPid = pid;
        } else {
            g_excInfo.phase = OS_EXC_IN_TASK;
            g_excInfo.thrdPid = g_runTask->taskId;
        }
    } else {
        g_excInfo.phase = OS_EXC_IN_INIT;
        g_excInfo.thrdPid = OS_NULL_INT;
    }
    if (excType & OS_EXC_FLAG_NO_FLOAT) {
        g_excInfo.context = (EXC_CONTEXT_S *)((CHAR *)excBufAddr - LOS_OFF_SET_OF(EXC_CONTEXT_S, uwR4));
    } else {
        g_excInfo.context = excBufAddr;
    }

#ifdef LOSCFG_SHELL_EXCINFO_DUMP
    LogReadWriteFunc dumpFunc = OsGetExcInfoRW();
    if (dumpFunc != NULL) {
        OsSetExcInfoOffset(0);
    }
#endif
    if (g_excRegHook != NULL) {
        g_excRegHook(excType, g_excInfo.context);
    } else {
        OsDoExcHook(EXC_INTERRUPT);
        OsExcInfoDisplay(&g_excInfo);
    }
#ifdef LOSCFG_SHELL_EXCINFO_DUMP
    if (dumpFunc != NULL) {
        dumpFunc(OsGetExcInfoDumpAddr(), OsGetExcInfoLen(), 0, OsGetExcInfoBuf());
    }
#endif
#ifdef LOSCFG_KERNEL_HPL
    OsHplExceptionHandle();
#endif
#if defined(LOSCFG_BOX) && !defined(LOSCFG_KERNEL_SMP) && !defined(LOSCFG_ARCH_ARM_V6M)
    OsContainBoxFault();
#endif
    ArchTaskExit();
}

#ifdef LOSCFG_BOX
WEAK LITE_OS_SEC_USER_RO_DATA UINT32 __stack_chk_guard = 0xd00a0dff;
#else
WEAK UINT32 __stack_chk_guard = 0xd00a0dff;
#endif

WEAK VOID __stack_chk_fail(VOID)
{
    /* __builtin_return_address is a builtin function, building in gcc */
    LOS_Panic("stack-protector: Kernel stack is corrupted in: %p\n",
              __builtin_return_address(0));
}
