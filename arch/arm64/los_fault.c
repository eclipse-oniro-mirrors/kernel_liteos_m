/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * AArch64 exception handling for liteos_m (simplified: single core, no
 * SMP halt IPI, no exc-recover), wired to the liteos_m HalExcHandleEntry
 * contract (see cortex_a_r/fault.c).
 */

#include "los_typedef.h"
#include "los_arch_interrupt.h"
#include "los_arch_context.h"
#include "los_task_pri.h"
#include "los_memory.h"
#include "los_exc.h"
#include "los_debug.h"
#include "los_printf_pri.h"

ExcInfo g_excInfo = {0};
UINT32 g_curNestCount = 0;

/* ESR_ELx exception class encodings (bits [31:26]). */
#define EC_SHIFT                        26
#define EC_MASK                         0x3FU
#define EC_UNKNOWN                      0x00
#define EC_WFX                          0x01
#define EC_CP15_32                      0x03
#define EC_CP15_64                      0x04
#define EC_CP14_32                      0x05
#define EC_CP14_64                      0x06
#define EC_SVC32                        0x11
#define EC_HVC32                        0x12
#define EC_SMC32                        0x13
#define EC_SVC64                        0x15
#define EC_HVC64                        0x16
#define EC_SMC64                        0x17
#define EC_SYS64                        0x18
#define EC_IABT_LOW                     0x20
#define EC_IABT_CUR                     0x21
#define EC_PC_ALIGN                     0x22
#define EC_DABT_LOW                     0x24
#define EC_DABT_CUR                     0x25
#define EC_SP_ALIGN                     0x26
#define EC_FP_EXC32                     0x28
#define EC_FP_EXC64                     0x2C
#define EC_SERROR                       0x2F

/* get bits [high:low] of val */
#define NBIT(val, high, low)            (((val) >> (low)) & ((1U << (((high) - (low)) + 1)) - 1))

#define DUMPREGS                        30

STATIC UINT64 FaultAddrGet(UINT32 bitsEC)
{
    UINT64 far;
    switch (bitsEC) {
        case EC_DABT_LOW:
        case EC_DABT_CUR:
        case EC_IABT_LOW:
        case EC_IABT_CUR:
            __asm__ volatile("mrs %0, far_el1" : "=r"(far));
            return far;
        default:
            return OS_EXC_IMPRECISE_ACCESS_ADDR;
    }
}

STATIC UINT32 ExcTypeGet(UINT32 bitsEC)
{
    switch (bitsEC) {
        case EC_UNKNOWN:
        case EC_FP_EXC32:
        case EC_FP_EXC64:
            return OS_EXC_CAUSE_UNDEF;
        case EC_IABT_LOW:
        case EC_IABT_CUR:
        case EC_PC_ALIGN:
            return OS_EXC_CAUSE_PABT;
        case EC_DABT_LOW:
        case EC_DABT_CUR:
        case EC_SP_ALIGN:
            return OS_EXC_CAUSE_DABT;
        case EC_SERROR:
            return OS_EXC_CAUSE_FATAL_ERR;
        default:
            return OS_EXC_CAUSE_UNUSED;
    }
}

STATIC VOID OsExcInfoDisplay(const ExcInfo *excInfo)
{
#if (LOSCFG_KERNEL_PRINTF != 0)
    CHAR *phaseStr[] = {"exc in init", "exc in task", "exc in hwi"};

    PRINTK("*************Exception Information**************\n");
    PRINTK("Type      = %u\n", excInfo->type);
    PRINTK("ThrdPid   = %u\n", excInfo->thrdPid);
    if (excInfo->phase <= OS_EXC_IN_HWI) {
        PRINTK("Phase     = %s\n", phaseStr[excInfo->phase]);
    }
    PRINTK("FaultAddr = 0x%llx\n", (UINT64)(UINTPTR)excInfo->faultAddr);
    PRINTK("nestCnt   = %u\n", excInfo->nestCnt);
    if (excInfo->context != NULL) {
        PRINTK("LR        = 0x%llx\n", excInfo->context->LR);
        PRINTK("PC        = 0x%llx\n", excInfo->context->uwPC);
        PRINTK("SPSR      = 0x%llx\n", excInfo->context->uwxPSR);
        for (UINT32 i = 0; i < DUMPREGS; i++) {
            PRINTK("X%-02u      = 0x%llx\n", i, excInfo->context->X[i]);
        }
    }
    if (excInfo->phase == OS_EXC_IN_TASK) {
        LosTaskCB *taskCB = OsCurrTaskGet();
        PRINTK("Task name = %s\n", taskCB->taskName);
        PRINTK("Task ID   = %u\n", taskCB->taskId);
    }
    UINT32 ret = LOS_MemIntegrityCheck(LOSCFG_SYS_HEAP_ADDR);
    if (ret == LOS_OK) {
        PRINTK("system heap memcheck passed!\n");
    }
#endif
}

#ifdef LOSCFG_MCS
/* PSCI 0.2 CPU_OFF function id (SMC32 calling convention). */
#define PSCI_FN_CPU_OFF 0x84000002UL

/* AMP slave exception exit: after the dump is complete (polling UART, no
 * IRQ dependency) the slave powers itself off via PSCI CPU_OFF so the
 * master can start it again - a half-dead slave (locks held, dead rpmsg
 * services) would stay "ON" and the next mica_start would fail with
 * "CPU already on". Self-contained: no board-level callback dependency. */
STATIC VOID ExcSlavePoweroff(VOID)
{
    PRINTK("[mica] exc slave poweroff: PSCI CPU_OFF\n");
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
    __asm__ volatile (
        "dsb  sy\n"
        "mov  x0, %0\n"
        "smc  #0\n"
        :
        : "r"((UINT64)PSCI_FN_CPU_OFF)
        : "x0", "memory");
    /* PSCI CPU_OFF does not return on success; park just in case. */
    for (;;) {
        __asm__ volatile ("wfe");
    }
}
#endif

/* ****************************************************************************
 * Function    : HalExcHandleEntry
 * Description : record exception info, run hooks, print and halt
 * **************************************************************************** */
LITE_OS_SEC_TEXT_INIT VOID HalExcHandleEntry(UINT32 excType, UINT64 faultAddr, UINT32 pid,
                                             EXC_CONTEXT_S *excBufAddr)
{
    UINT16 tmpFlag = (UINT16)((excType >> 16) & OS_NULL_SHORT);

    g_curNestCount++;
    g_excInfo.nestCnt++;

    g_excInfo.type = (UINT16)(excType & OS_NULL_SHORT);

    if (tmpFlag & OS_EXC_FLAG_FAULTADDR_VALID) {
        g_excInfo.faultAddr = faultAddr;
    } else {
        g_excInfo.faultAddr = OS_EXC_IMPRECISE_ACCESS_ADDR;
    }

    LosTaskCB *runTask = OsCurrTaskGet();
    if (runTask != NULL) {
        if (tmpFlag & OS_EXC_FLAG_IN_HWI) {
            g_excInfo.phase = OS_EXC_IN_HWI;
            g_excInfo.thrdPid = pid;
        } else {
            g_excInfo.phase = OS_EXC_IN_TASK;
            g_excInfo.thrdPid = runTask->taskId;
        }
    } else {
        g_excInfo.phase = OS_EXC_IN_INIT;
        g_excInfo.thrdPid = OS_NULL_INT;
    }
    g_excInfo.context = excBufAddr;

    if (g_excRegHook != NULL) {
        g_excRegHook(excType, g_excInfo.context);
    } else {
        OsDoExcHook(EXC_INTERRUPT);
        OsExcInfoDisplay(&g_excInfo);
    }
#ifdef LOSCFG_MCS
    /* AMP: the dump is complete (polling UART, no IRQ dependency) - power
     * off instead of exiting the task; never returns. */
    ExcSlavePoweroff();
#else
    ArchTaskExit();
#endif
}


/* ****************************************************************************
 * Function    : OsArm64SyncExcHandler
 * Description : sync/serror exception entry from los_exc.S (frame on SP).
 *               Decodes ESR_EL1, feeds the standard HalExcHandleEntry
 *               contract (Exception Information dump: type, fault address,
 *               register set, current task, heap check) and halts;
 *               never returns.
 * **************************************************************************** */
LITE_OS_SEC_TEXT VOID OsArm64SyncExcHandler(EXC_CONTEXT_S *frame)
{
    UINT64 esr64;
    UINT64 far;
    UINT32 ec;
    UINT32 excType;
    UINT32 flags = 0;

    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr64));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));
    ec = (UINT32)((esr64 >> EC_SHIFT) & EC_MASK);

    excType = ExcTypeGet(ec);
    if ((ec == EC_DABT_LOW) || (ec == EC_DABT_CUR) ||
        (ec == EC_IABT_LOW) || (ec == EC_IABT_CUR)) {
        flags = OS_EXC_FLAG_FAULTADDR_VALID;
    }

    HalExcHandleEntry(excType | (flags << 16), far, 0, frame);
    /* HalExcHandleEntry never returns (ArchTaskExit); park just in case. */
    for (;;) {
        __asm__ volatile("wfe");
    }
}

/* ****************************************************************************
 * Function    : OsCurrentELGet
 * Description : helper called by the los_exc.S vector slots: the
 *               CurrentEL value is passed in reg; print the EL before the
 *               slot parks (NBIT above has the bit-field semantics).
 * **************************************************************************** */
VOID OsCurrentELGet(UINT32 reg)
{
    PRINT_EXC("The current exception level is EL%u\n", NBIT(reg, 3, 2));
}

/* ****************************************************************************
 * Function    : OsExceptSyncExcHdl
 * Description : sync/serror entry name used by los_exc.S; the frame
 *               contract is identical (EXC_CONTEXT_S layout), so wire it
 *               straight onto this tree's handler.
 * **************************************************************************** */
VOID OsExceptSyncExcHdl(EXC_CONTEXT_S *frame)
{
    OsArm64SyncExcHandler(frame);
}
