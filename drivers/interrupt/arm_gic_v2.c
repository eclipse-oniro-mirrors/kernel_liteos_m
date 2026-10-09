/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: ARM GICv2 interrupt controller driver (single core, Core 0).
 *              SoC-agnostic: GIC base addresses come from the board's soc.h
 *              (GIC_BASE_ADDR, GICD_OFFSET, GICC_OFFSET) via los_arch_interrupt.h.
 *              Register access is through the GICD / GICC struct pointers.
 * Reference: arm document IHI0048B/b — GIC Architecture Specification.
 */

#include "los_arch_interrupt.h"
#include "los_arch_context.h"
#include "gic_v2.h"
#ifdef LOSCFG_KERNEL_SMP
#include "los_mp_pri.h"
#endif
#include "los_hwi_pri.h"
#include "los_debug.h"
#if (LOSCFG_CPUP_INCLUDE_IRQ == 1)
#include "los_cpup_pri.h"
#endif

volatile UINT32 g_curIrqNum[LOSCFG_KERNEL_CORE_NUM] = {0};


STATIC UINT32 HwiNumGet(VOID)
{
    return g_curIrqNum[ArchCurrCpuid()];
}

STATIC UINT32 HwiUnmask(HWI_HANDLE_T hwiNum)
{
    GICD->isenabler[hwiNum / INT_COUNTS_32_PER_REG] = (1U << (hwiNum % INT_COUNTS_32_PER_REG));
    return LOS_OK;
}

STATIC UINT32 HwiMask(HWI_HANDLE_T hwiNum)
{
    GICD->icenabler[hwiNum / INT_COUNTS_32_PER_REG] = (1U << (hwiNum % INT_COUNTS_32_PER_REG));
    return LOS_OK;
}

STATIC UINT32 HwiSetPriority(HWI_HANDLE_T hwiNum, HWI_PRIOR_T priority)
{
    UINT32 offset = hwiNum / INT_COUNTS_4_PER_REG;
    UINT32 shift = (hwiNum % INT_COUNTS_4_PER_REG) * 8;
    UINT32 val;

    val = GICD->ipriorityr[offset];
    val &= ~(0xFFU << shift);
    val |= ((priority & 0xFF) << shift);
    GICD->ipriorityr[offset] = val;
    return LOS_OK;
}

STATIC UINT32 HwiPending(HWI_HANDLE_T hwiNum)
{
    if (hwiNum < GIC_MIN_PPI_NUM) {
        /* SGI (0-15): use SGIR to soft-pend (target CPU 0, filter=0).
         * PPIs (16-31) must NOT take this path — SGIR can only generate
         * SGI IDs; they go through ISPENDR below. */
        GICD->sgir = (0U << 24) | (1U << 16) | (hwiNum & 0xF);
    } else {
        /* PPI (16-31) + SPI (32+): set pending via ISPENDR. */
        GICD->ispendr[hwiNum / INT_COUNTS_32_PER_REG] = (1U << (hwiNum % INT_COUNTS_32_PER_REG));
    }
    return LOS_OK;
}

STATIC UINT32 HwiClear(HWI_HANDLE_T hwiNum)
{
    GICD->icpendr[hwiNum / INT_COUNTS_32_PER_REG] = (1U << (hwiNum % INT_COUNTS_32_PER_REG));
    return LOS_OK;
}

#ifdef LOSCFG_KERNEL_SMP
#define GIC_SGI_INTID_MASK       0xFU
#define GIC_SGI_FILTER_MASK      0x3U
#define GIC_SGI_FILTER_SHIFT     24
#define GIC_SGI_TARGET_MASK      0xFFU
#define GIC_SGI_TARGET_SHIFT     16

STATIC VOID GicWriteSgi(UINT32 intId, UINT32 cpuMask, UINT32 filter)
{
    UINT32 val = ((filter & GIC_SGI_FILTER_MASK) << GIC_SGI_FILTER_SHIFT) |
                 ((cpuMask & GIC_SGI_TARGET_MASK) << GIC_SGI_TARGET_SHIFT) |
                 (intId & GIC_SGI_INTID_MASK);

    GICD->sgir = val;
}

STATIC UINT32 HalIrqSendIpi(HWI_HANDLE_T hwiNum, UINT32 cpuMask)
{
    if (hwiNum > GIC_SGI_INTID_MASK) {
        return LOS_ERRNO_HWI_NUM_INVALID;
    }
    if ((cpuMask == 0) || (cpuMask > LOSCFG_KERNEL_CPU_MASK)) {
        return LOS_ERRNO_HWI_ARG_NOT_ENABLED;
    }
    GicWriteSgi(hwiNum, cpuMask, 0);
    return LOS_OK;
}

STATIC UINT32 HalIrqSetAffinity(HWI_HANDLE_T hwiNum, UINT32 cpuMask)
{
    volatile UINT8 *targetReg;

    if (hwiNum >= OS_HWI_MAX_NUM) {
        return LOS_ERRNO_HWI_NUM_INVALID;
    }
    if (((cpuMask & LOSCFG_KERNEL_CPU_MASK) != cpuMask) || (cpuMask > 0xFF)) {
        return LOS_ERRNO_HWI_AFFI_INVALID;
    }
    targetReg = ((volatile UINT8 *)&GICD->itargetsr[hwiNum / INT_COUNTS_4_PER_REG]) +
                (hwiNum % INT_COUNTS_4_PER_REG);
    *targetReg = (UINT8)cpuMask;
    return LOS_OK;
}
#endif

STATIC HwiControllerOps g_hwiControllerOps = {
    .enableIrq = HwiUnmask,
    .disableIrq = HwiMask,
    .setIrqPriority = HwiSetPriority,
    .getCurIrqNum = HwiNumGet,
    .triggerIrq = HwiPending,
    .clearIrq = HwiClear,
    .getHandleForm = HalGetHandleForm,
#ifdef LOSCFG_KERNEL_SMP
    .sendIpi = HalIrqSendIpi,
    .setIrqCpuAffinity = HalIrqSetAffinity,
#endif
};

HwiControllerOps *HwiControllerOpsGet(VOID)
{
    return &g_hwiControllerOps;
}

/* ****************************************************************************
 Function    : HalInterrupt
 Description : Hardware interrupt entry function (called by OsIrqHandler in asm)
               Reads IAR, dispatches via OsIntHandle, writes EOI.
 **************************************************************************** */
LITE_OS_SEC_TEXT VOID HalInterrupt(VOID)
{
    UINT32 iar = GICC->iar;
    UINT32 curIrq = iar & GICC_IAR_INTID_MASK;
    g_curIrqNum[ArchCurrCpuid()] = curIrq;
    UINT32 hwiIndex = curIrq;
    if (hwiIndex >= OS_HWI_MAX_NUM) {
        GICC->eoir = iar;
        return;
    }
    HwiHandleInfo *handleForm = (HwiHandleInfo *)HalGetHandleForm(hwiIndex);

#if (LOSCFG_CPUP_INCLUDE_IRQ == 1)
    OsCpupIrqStart(OS_HWI_INVALID_IRQ, hwiIndex);
#endif
    OsIntHandle(hwiIndex, handleForm);
#if (LOSCFG_CPUP_INCLUDE_IRQ == 1)
    OsCpupIrqEnd(OS_HWI_INVALID_IRQ, hwiIndex);
#endif

    GICC->eoir = iar;
}

/* stack protector */
WEAK UINT32 __stack_chk_guard = 0xd00a0dff;

WEAK VOID __stack_chk_fail(VOID)
{
    LOS_Panic("stack-protector: Kernel stack is corrupted in: %p\n",
              __builtin_return_address(0));
}

LITE_OS_SEC_TEXT_INIT VOID HalIrqInitPercpu(VOID)
{
    /* unmask interrupts */
    GICC->pmr = 0xFF;

    /* enable gic cpu interface */
    GICC->ctlr = 1;
#ifdef LOSCFG_KERNEL_SMP
    HwiUnmask(LOS_MP_IPI_WAKEUP);
    HwiUnmask(LOS_MP_IPI_SCHEDULE);
    HwiUnmask(LOS_MP_IPI_HALT);
#endif
}

/* ****************************************************************************
 Function    : HalHwiInit
 Description : initialization of GICv2 (single core, Core 0 only).
               - Disables distributor & CPU interface
               - Reads GICD_TYPER for actual IRQ count
               - Routes all SPIs to CPU 0, sets default priority, disables all
               - Enables CPU interface (PMR=0xFF) and distributor
               - Installs default vector handlers
 **************************************************************************** */
LITE_OS_SEC_TEXT_INIT VOID HalHwiInit(VOID)
{
    UINT32 i;
    UINT32 irqNum;

    GICD->ctlr = 0;
    GICC->ctlr = 0;

    /* GICD_TYPER bit[4:0]: (number of SPIs / 32) - 1; total = (ITLines+1)*32 */
    irqNum = ((GICD->typer & 0x1F) + 1) * INT_COUNTS_32_PER_REG;
    if (irqNum > OS_HWI_MAX_NUM) {
        irqNum = OS_HWI_MAX_NUM;
    }

    /* route all SPIs to CPU 0 */
    for (i = 0; i < irqNum / INT_COUNTS_4_PER_REG; i++) {
        GICD->itargetsr[i] = TARGET_CPU_MASK;
    }
    /* set default priority on all interrupts */
    for (i = 0; i < irqNum / INT_COUNTS_4_PER_REG; i++) {
        GICD->ipriorityr[i] = GICD_INT_DEF_PRI_X4;
    }
    /* disable all interrupts */
    for (i = 0; i < irqNum / INT_COUNTS_32_PER_REG; i++) {
        GICD->icenabler[i] = 0xFFFFFFFF;
    }
    /* clear all pending */
    for (i = 0; i < irqNum / INT_COUNTS_32_PER_REG; i++) {
        GICD->icpendr[i] = 0xFFFFFFFF;
    }

    HalIrqInitPercpu();

    /* enable distributor */
    GICD->ctlr = 1;

    HWI_PROC_FUNC *hwiForm = (HWI_PROC_FUNC *)ArchGetHwiFrom();
    for (i = 0; i < OS_VECTOR_CNT; i++) {
        hwiForm[i] = (HWI_PROC_FUNC)HalHwiDefaultHandler;
    }

#ifdef LOSCFG_KERNEL_SMP
    (VOID)LOS_HwiCreate(LOS_MP_IPI_WAKEUP, OS_HWI_PRIO_HIGHEST, 0, (HWI_PROC_FUNC)OsMpWakeHandler, NULL);
    (VOID)LOS_HwiCreate(LOS_MP_IPI_SCHEDULE, OS_HWI_PRIO_HIGHEST, 0, (HWI_PROC_FUNC)OsMpScheduleHandler, NULL);
    (VOID)LOS_HwiCreate(LOS_MP_IPI_HALT, OS_HWI_PRIO_HIGHEST, 0, (HWI_PROC_FUNC)OsMpHaltHandler, NULL);
#endif
}
