/*
 * Copyright (c) 2026 morpheus
 * GICv3 interrupt controller driver (liteos_m los_hwi contract),
 * ported from hs-fbb drivers/interrupt/arm_gic_v3.c and adapted for the
 * RK3568 AMP deployment:
 *
 *  - CPU3 runs liteos_m at EL1 (non-secure) while Linux owns CPU0-2;
 *  - the GICD is shared with Linux: it is NOT re-initialized here. Per the
 *    ownership contract, each core configures the GICD entries of the SPIs
 *    it owns: createIrq() sets group (Group1-NS), route (own core) plus
 *    priority/enable, and setIrqMode() programs the trigger (hwiMode) into
 *    ICFGR, touching only that IRQ's bit fields;
 *    handed-over INTIDs must not be claimed by any Linux-side driver;
 *  - this core's Redistributor and CPU Interface (ICC_* system regs) are
 *    private and freely configured;
 *  - EL3/SIC/LPI paths of the original driver are stripped.
 */

#include <stdarg.h>
#include "securec.h"
#include "los_context.h"
#include "los_arch_interrupt.h"
#include "los_arch_context.h"
#include "los_hwi_pri.h"
#include "los_task_pri.h"
#include "los_sched.h"
#include "los_memory.h"
#include "los_exc.h"
#include "los_typedef.h"
#include "los_debug.h"
#if (LOSCFG_CPUP_INCLUDE_IRQ == 1)
#include "los_cpup_pri.h"
#endif

volatile UINT32 g_curIrqNum = 0;

/* Redistributor slot of this core, resolved in HalHwiInit. */
STATIC UINT32 g_gicrSlot = 0;

/* ----------------------------------------------------------------------------
 * Redistributor helpers.
 * -------------------------------------------------------------------------- */
STATIC INLINE UINT32 MpidrGet(VOID)
{
    UINT64 mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (UINT32)mpidr;
}

STATIC INLINE UINT64 MpidrGet64(VOID)
{
    UINT64 mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return mpidr;
}

STATIC UINT32 GicrSlotGet(VOID)
{
    UINT32 mpidr = MpidrGet();
    /* RK3568: 4 single-core clusters, core number is in MPIDR Aff1 (Aff0 always 0).
     * rdist slots linear: slot n at GICR_BASE + n * 0x20000. */
    return (mpidr >> 8) & 0xFF;
}

STATIC INLINE VOID GicrWaitForRwp(UINT32 slot)
{
    INT32 count = 1000000; /* 1s */

    while (GICR_REG32(GICR_CTLR(slot)) & GICD_CTLR_RWP) {
        count--;
        if (count == 0) {
            PRINTK("gic_v3: GICR rwp timeout, slot %u\n", slot);
            return;
        }
    }
}

STATIC INLINE VOID GicdWaitForRwp(VOID)
{
    INT32 count = 1000000; /* 1s */

    while (GICD_REG32(GICD_CTLR) & GICD_CTLR_RWP) {
        count--;
        if (count == 0) {
            PRINTK("gic_v3: GICD rwp timeout\n");
            return;
        }
    }
}

/* ----------------------------------------------------------------------------
 * HwiControllerOps callbacks.
 * -------------------------------------------------------------------------- */
STATIC UINT32 HwiNumGet(VOID)
{
    return g_curIrqNum;
}

STATIC UINT32 HwiUnmask(HWI_HANDLE_T hwiNum)
{
    UINT32 mask = 1U << (hwiNum % 32);

    if (hwiNum < GIC_MIN_SPI_NUM) {
        /* SGI/PPI: our own Redistributor. */
        GICR_REG32(GICR_ISENABLER0(g_gicrSlot)) = mask;
        GicrWaitForRwp(g_gicrSlot);
    } else {
        GICD_REG32(GICD_ISENABLER(hwiNum / 32)) = mask;
        GicdWaitForRwp();
    }
    return LOS_OK;
}

STATIC UINT32 HwiMask(HWI_HANDLE_T hwiNum)
{
    UINT32 mask = 1U << (hwiNum % 32);

    if (hwiNum < GIC_MIN_SPI_NUM) {
        GICR_REG32(GICR_ICENABLER0(g_gicrSlot)) = mask;
        GicrWaitForRwp(g_gicrSlot);
    } else {
        GICD_REG32(GICD_ICENABLER(hwiNum / 32)) = mask;
        GicdWaitForRwp();
    }
    return LOS_OK;
}

STATIC UINT32 HwiSetPriority(HWI_HANDLE_T hwiNum, HWI_PRIOR_T hwiPrio)
{
    UINT32 offset = hwiNum / 4;
    UINT32 shift = (hwiNum % 4) * 8;
    UINT32 prio = GIC_INT_PRI(hwiPrio);
    UINT32 val;

    if (hwiNum < GIC_MIN_SPI_NUM) {
        val = GICR_REG32(GICR_IPRIORITYR(g_gicrSlot, offset));
        val &= ~(GIC_PRIORITY_MASK << shift);
        val |= prio << shift;
        GICR_REG32(GICR_IPRIORITYR(g_gicrSlot, offset)) = val;
        GicrWaitForRwp(g_gicrSlot);
    } else {
        val = GICD_REG32(GICD_IPRIORITYR(offset));
        val &= ~(GIC_PRIORITY_MASK << shift);
        val |= prio << shift;
        GICD_REG32(GICD_IPRIORITYR(offset)) = val;
        GicdWaitForRwp();
    }
    return LOS_OK;
}

STATIC UINT32 HwiPending(HWI_HANDLE_T hwiNum)
{
    UINT32 mask = 1U << (hwiNum % 32);

    if (hwiNum < 16) {
        /* SGI: raise on this core via ICC_SGI1R_EL1 (target list = us). */
        UINT32 mpidr = MpidrGet();
        UINT64 sgi1r = ((UINT64)hwiNum << 24) |
                       (((mpidr >> 8) & 0xFF) << 16) |
                       (1ULL << (mpidr & 0xFF));
        GiccSetSgi1r(sgi1r);
    } else if (hwiNum < GIC_MIN_SPI_NUM) {
        GICR_REG32(GICR_ISPENDR0(g_gicrSlot)) = mask;
    } else {
        GICD_REG32(GICD_ISPENDR(hwiNum / 32)) = mask;
    }
    return LOS_OK;
}

STATIC UINT32 HwiClear(HWI_HANDLE_T hwiNum)
{
    UINT32 mask = 1U << (hwiNum % 32);

    if (hwiNum < GIC_MIN_SPI_NUM) {
        GICR_REG32(GICR_ICPENDR0(g_gicrSlot)) = mask;
    } else {
        GICD_REG32(GICD_ICPENDR(hwiNum / 32)) = mask;
    }
    return LOS_OK;
}

/* AMP ownership contract: an SPI created by this core belongs to it. The GICD
 * is shared with Linux - only this IRQ's own bit fields are touched, and the
 * INTID must not be claimed by any Linux-side driver (business convention:
 * handed-over SPIs stay disabled in the Linux device tree).
 *  - group: Group1 non-secure (delivered as IRQ at EL1 NS; Group0 = FIQ)
 *  - route: IROUTER <- this core's MPIDR affinity
 *  - trigger: ICFGR <- setIrqMode op (HWI_MODE_TRIGGER_EDGE / _LEVEL)
 * Idempotent: re-asserting the same values is side-effect free, so repeated
 * HwiCreate after HwiDelete simply re-establishes the contract. */
STATIC VOID HwiSpiConfigOwn(HWI_HANDLE_T hwiNum)
{
    UINT32 bit = 1U << (hwiNum % 32);
    UINT32 val;

    val = GICD_REG32(GICD_IGROUPR(hwiNum / 32));
    if ((val & bit) == 0) {
        GICD_REG32(GICD_IGROUPR(hwiNum / 32)) = val | bit;
        GicdWaitForRwp();
    }

    /* MPIDR Aff0..Aff3 sit at the same bit positions as in IROUTER
     * (Aff3[39:32] Aff2[23:16] Aff1[15:8] Aff0[7:0]; MT bit 24 masked out). */
    GICD_REG64(GICD_IROUTER(hwiNum)) = MpidrGet64() & 0xFF00FFFFFFULL;
}

/* setIrqMode op: ICFGR carries the SPI trigger config (2 bits per INTID,
 * bit1 = edge-sensitive, bit0 reserved). SGI/PPI have no ICFGR - the GIC
 * architecture fixes their trigger, so the op is a no-op for them. */
STATIC UINT32 HwiSetIrqMode(HWI_HANDLE_T hwiNum, HWI_MODE_T hwiMode)
{
    UINT32 bit;
    UINT32 val;

    if (hwiNum < GIC_MIN_SPI_NUM) {
        return LOS_OK;
    }

    bit = 1U << ((hwiNum % 16) * 2 + 1);
    val = GICD_REG32(GICD_ICFGR(hwiNum / 16));
    if ((hwiMode & HWI_MODE_TRIGGER_EDGE) != 0) {
        val |= bit;
    } else {
        val &= ~bit;
    }
    GICD_REG32(GICD_ICFGR(hwiNum / 16)) = val;
    GicdWaitForRwp();
    return LOS_OK;
}

STATIC UINT32 HwiCreate(HWI_HANDLE_T hwiNum, HWI_PRIOR_T hwiPrio)
{
    if (hwiNum >= GIC_MIN_SPI_NUM) {
        HwiSpiConfigOwn(hwiNum);
    }
    HwiSetPriority(hwiNum, hwiPrio);
    HwiUnmask(hwiNum);
    return LOS_OK;
}

STATIC HwiControllerOps g_archHwiOps = {
    .enableIrq = HwiUnmask,
    .disableIrq = HwiMask,
    .setIrqPriority = HwiSetPriority,
    .getCurIrqNum = HwiNumGet,
    .triggerIrq = HwiPending,
    .clearIrq = HwiClear,
    .createIrq = HwiCreate,
    .setIrqMode = HwiSetIrqMode,
    .getHandleForm = HalGetHandleForm,
};

HwiControllerOps *ArchIntOpsGet(VOID)
{
    return &g_archHwiOps;
}

/* Legacy name used by kernel/base/los_hwi.c (LOS_Hwi* API layer). */
HwiControllerOps *HwiControllerOpsGet(VOID)
{
    return &g_archHwiOps;
}

/* ----------------------------------------------------------------------------
 * SGI towards another core (Linux CPU0), used by OpenAMP virtio_notify.
 * Only SGI 0-15, Group-1 NS, target affinity from MPIDR (single cluster).
 * -------------------------------------------------------------------------- */
VOID HalIrqSendSgiToCore(UINT32 hwiNum, UINT32 targetCpuMpidrAff0)
{
    UINT64 sgi1r = ((UINT64)(hwiNum & 0xF) << 24) | (1ULL << (targetCpuMpidrAff0 & 0xFF));
    GiccSetSgi1r(sgi1r);
}

/* ****************************************************************************
 * Function    : HalInterrupt
 * Description : IRQ entry (called from _osExceptIrqHdl). Reads the active
 *               INTID through ICC_IAR1_EL1, dispatches via the kernel table
 *               and signals EOI.
 * **************************************************************************** */
LITE_OS_SEC_TEXT VOID HalInterrupt(VOID)
{
    UINT32 iar = GiccGetIar();
    UINT32 hwiIndex = iar & 0x3FF; /* 1023 = spurious */

    if (hwiIndex >= OS_HWI_MAX_NUM) {
        (VOID)iar;
        GiccSetEoir(iar);
        return;
    }
    g_curIrqNum = hwiIndex;

    HwiHandleInfo *handleForm = (HwiHandleInfo *)HalGetHandleForm(hwiIndex);

#if (LOSCFG_CPUP_INCLUDE_IRQ == 1)
    OsCpupIrqStart(OS_HWI_INVALID_IRQ, hwiIndex);
#endif
    OsIntHandle(hwiIndex, handleForm);
#if (LOSCFG_CPUP_INCLUDE_IRQ == 1)
    OsCpupIrqEnd(OS_HWI_INVALID_IRQ, hwiIndex);
#endif

    /* EOI + deactivate (EOI mode 0, ICC_CTLR_EL1.EOImode == 0) */
    GiccSetEoir(iar);
    g_curIrqNum = 0;
}

/* stack protector */
WEAK UINT32 __stack_chk_guard = 0xd00a0dff;

WEAK VOID __stack_chk_fail(VOID)
{
    LOS_Panic("stack-protector: Kernel stack is corrupted in: %p\n",
              __builtin_return_address(0));
}

/* ****************************************************************************
 * Function    : HalHwiInit
 * Description : GICv3 bring-up, AMP-safe:
 *               1. resolve our Redistributor slot;
 *               2. wake our Redistributor and configure SGI/PPI as Group-1
 *                  NS, default priority, all disabled;
 *               3. enable the CPU Interface (ICC_SRE/PMR/BPR/IGRPEN1).
 *               The GICD is left as configured by Linux/TF-A.
 * **************************************************************************** */
LITE_OS_SEC_TEXT_INIT VOID HalHwiInit(VOID)
{
    UINT32 i;
    UINT32 slot;
    HWI_PROC_FUNC *hwiForm = (HWI_PROC_FUNC *)ArchGetHwiFrom();

    slot = GicrSlotGet();
    g_gicrSlot = slot;

    /* ---- CPU interface ---- */
    UINT64 sre;
    sre = GiccGetSre();
    if ((sre & 0x1) == 0) {
        GiccSetSre(sre | 0x1);
        sre = GiccGetSre();
        if ((sre & 0xF) == 0) {
            PRINT_ERR("GICv3: ICC_SRE_EL1 disabled by higher EL, "
                      "check TF-A/EL2 config\n");
        }
    }

    GiccSetPmr(0xFF);        /* unmask all priorities */
    GiccSetBpr1(MAX_BINARY_POINT_VALUE); /* no nested group preemption */
    /* SGI/PPI group: Group-1 non-secure (matches Linux's view). */
    GiccSetIgrpen1(1);       /* enable Group-1 interrupts */

    /* ---- our Redistributor ---- */
    GICR_REG32(GICR_WAKER(slot)) &= ~GICR_WAKER_PROCESSORSLEEP;
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
    while ((GICR_REG32(GICR_WAKER(slot)) & GICR_WAKER_CHILDRENASLEEP) != 0) { }

    /* SGI/PPI group: Group-1 non-secure pairing (IGROUPR0=1, IGRPMODR0=0).
     * On this DS=0 GIC both registers are RAZ/WI for non-secure access, so
     * these writes are dropped (measured); keep them for correctness on
     * single-security-state GICs and so the group is right the moment the
     * firmware provides it. */
    GICR_REG32(GICR_IGROUPR0(slot)) = 0xFFFFFFFF;
    GICR_REG32(GICR_IGROUPMOD0_ADDR(slot)) = 0x0;

    /* default priority for SGI/PPI */
    for (i = 0; i < GIC_MIN_SPI_NUM; i++) {
        UINT32 offset = i / 4;
        UINT32 shift = (i % 4) * 8;
        UINT32 val = GICR_REG32(GICR_IPRIORITYR(slot, offset));
        val &= ~(GIC_PRIORITY_MASK << shift);
        val |= (UINT32)GIC_INT_DEF_PRI << shift;
        GICR_REG32(GICR_IPRIORITYR(slot, offset)) = val;
    }

    /* disable + clear-pending all SGI/PPI on our core */
    GICR_REG32(GICR_ICENABLER0(slot)) = 0xFFFFFFFF;
    GICR_REG32(GICR_ICPENDR0(slot)) = 0xFFFFFFFF;
    GICR_REG32(GICR_ICACTIVER0(slot)) = 0xFFFFFFFF;
    GicrWaitForRwp(slot);

    /* ---- software dispatch table ---- */
    for (i = 0; i < OS_VECTOR_CNT; i++) {
        hwiForm[i] = (HWI_PROC_FUNC)HalHwiDefaultHandler;
    }
}
