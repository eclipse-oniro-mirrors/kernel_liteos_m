/*
 * Copyright (c) 2026 morpheus
 * AArch64 arch interrupt header (GICv3, EL1 non-secure view).
 * Ported from hs-fbb arch_lite/arm64 + drivers/interrupt/arm_gic_v3.h,
 * adapted to the liteos_m los_hwi contract (see gic-v2/interrupt.c).
 */

#ifndef _LOS_ARCH_INTERRUPT_H
#define _LOS_ARCH_INTERRUPT_H

#include "los_common_interrupt.h"
#include "los_compiler.h"
#include "soc.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* LiteOS priority range: 0 (highest) .. 31 (lowest), mapped onto the
 * GICv3 8-bit priority field by LOSCFG_HWI_PRIO_LIMIT shift. */
#ifndef OS_HWI_PRIO_HIGHEST
#define OS_HWI_PRIO_HIGHEST                   0
#endif

#ifndef OS_HWI_PRIO_LOWEST
#define OS_HWI_PRIO_LOWEST                    31
#endif

#define OS_SYS_VECTOR_CNT                     0
#define OS_VECTOR_CNT                         (OS_SYS_VECTOR_CNT + OS_HWI_MAX_NUM)

/* Kept for los_common_interrupt.c (ArchHwiDelete cast). */
typedef INT32 IRQn_Type;

/* Interrupt number classes (GICv3 INTIDs). */
#define GIC_MIN_SPI_NUM                       32

/* HWI_MODE_T trigger encoding for SPIs (bit0; level is the reset default).
 * The gic-v3 port writes it into GICD_ICFGR at create time. SGIs/PPIs have
 * no ICFGR and ignore these bits. */
#define HWI_MODE_TRIGGER_LEVEL                0x0000
#define HWI_MODE_TRIGGER_EDGE                 0x0001

/* Slave tick source: PPI 30 (CNTP, ARM generic EL1 physical timer).
 * The GIC default priority 0xf0 equals the slave's projected ICC_PMR_EL1
 * (PRIbits=5), which would keep the tick pending-forever; los_timer.c
 * rewrites the INTID 30 priority to 0x00 at init (non-secure writes to
 * IPRIORITYR do land - same fix as the rpmsg SGI 7 kick).
 * Board-verified with the plain maxcpus=3 image: CPU3 never goes through
 * the Linux secondary bring-up chain and PPI 30 is still delivered, so no
 * boot-image or bootargs coupling is required. */
#define OS_GIC_TIMER_IRQ                      30

/* ============================================================================
 * CPU interface (ICC_*) system registers, EL1 non-secure.
 * ==========================================================================*/
#define ICC_CTLR_EL1                          "S3_0_C12_C12_4"
#define ICC_PMR_EL1                           "S3_0_C4_C6_0"
#define ICC_IAR1_EL1                          "S3_0_C12_C12_0"
#define ICC_EOIR1_EL1                         "S3_0_C12_C12_1"
#define ICC_SRE_EL1                           "S3_0_C12_C12_5"
#define ICC_BPR1_EL1                          "S3_0_C12_C12_3"
#define ICC_IGRPEN1_EL1                       "S3_0_C12_C12_7"
#define ICC_SGI1R_EL1                         "S3_0_C12_C11_5"

STATIC INLINE UINT32 GiccGetSre(VOID)
{
    UINT64 val;
    __asm__ volatile("mrs %0, " ICC_SRE_EL1 : "=r"(val));
    return (UINT32)val;
}

STATIC INLINE VOID GiccSetSre(UINT32 val)
{
    __asm__ volatile("msr " ICC_SRE_EL1 ", %0" ::"r"((UINT64)val));
    __asm__ volatile("isb" ::: "memory");
}

STATIC INLINE UINT32 GiccGetIar(VOID)
{
    UINT64 val;
    __asm__ volatile("mrs %0, " ICC_IAR1_EL1 : "=r"(val));
    return (UINT32)val;
}

STATIC INLINE VOID GiccSetEoir(UINT32 val)
{
    __asm__ volatile("msr " ICC_EOIR1_EL1 ", %0" ::"r"((UINT64)val));
    __asm__ volatile("isb" ::: "memory");
}

STATIC INLINE VOID GiccSetPmr(UINT32 val)
{
    __asm__ volatile("msr " ICC_PMR_EL1 ", %0" ::"r"((UINT64)val));
    __asm__ volatile("isb" ::: "memory");
}

STATIC INLINE VOID GiccSetBpr1(UINT32 val)
{
    __asm__ volatile("msr " ICC_BPR1_EL1 ", %0" ::"r"((UINT64)val));
    __asm__ volatile("isb" ::: "memory");
}

STATIC INLINE VOID GiccSetIgrpen1(UINT32 val)
{
    __asm__ volatile("msr " ICC_IGRPEN1_EL1 ", %0" ::"r"((UINT64)val));
    __asm__ volatile("isb" ::: "memory");
}

/* Generate a Group-1 SGI. val encodes target list + INTID (see GICv3 spec). */
STATIC INLINE VOID GiccSetSgi1r(UINT64 val)
{
    __asm__ volatile("msr " ICC_SGI1R_EL1 ", %0" ::"r"(val));
    __asm__ volatile("isb" ::: "memory");
}

/* ============================================================================
 * Distributor (GICD) - shared with Linux. Only per-IRQ registers are touched.
 * ==========================================================================*/
#define GICD_REG32(off)                       (*(volatile UINT32 *)(UINTPTR)(GICD_BASE + (UINTPTR)(off)))
#define GICD_REG64(off)                       (*(volatile UINT64 *)(UINTPTR)(GICD_BASE + (UINTPTR)(off)))

#define GICD_CTLR                             0x0000
#define GICD_TYPER                            0x0004
#define GICD_ISENABLER(n)                     (0x0100 + (n) * 4)
#define GICD_ICENABLER(n)                     (0x0180 + (n) * 4)
#define GICD_ISPENDR(n)                       (0x0200 + (n) * 4)
#define GICD_ICPENDR(n)                       (0x0280 + (n) * 4)
#define GICD_IPRIORITYR(n)                    (0x0400 + (n) * 4)
#define GICD_ICFGR(n)                         (0x0c00 + (n) * 4)
#define GICD_IGROUPR(n)                       (0x0080 + (n) * 4)
#define GICD_IROUTER(n)                       (0x6000 + (n) * 8)
#define GICD_PIDR2                            0xffe8

#define GICD_CTLR_RWP                         (1U << 31)

/* ============================================================================
 * Redistributor (GICR) - per core, our own. Two 64KB frames per core:
 * RD frame (offset 0x0000) and SGI/PPI frame (offset 0x10000).
 * ==========================================================================*/
#define GICR_SGI_OFFSET                         0x10000
#define GICR_STRIDE                           0x20000

#define GICR_REG32(off)                       (*(volatile UINT32 *)(UINTPTR)(GICR_BASE + (UINTPTR)(off)))
#define GICR_REG64(off)                       (*(volatile UINT64 *)(UINTPTR)(GICR_BASE + (UINTPTR)(off)))

/* RD frame registers (i = redistributor slot index). */
#define GICR_CTLR(i)                          (GICR_STRIDE * (i) + 0x0000)
#define GICR_IIDR(i)                          (GICR_STRIDE * (i) + 0x0004)
#define GICR_TYPER(i)                         (GICR_STRIDE * (i) + 0x0008)
#define GICR_WAKER(i)                         (GICR_STRIDE * (i) + 0x0014)

#define GICR_WAKER_PROCESSORSLEEP             (1U << 1)
#define GICR_WAKER_CHILDRENASLEEP             (1U << 2)

/* GICR_TYPER bits (GICv3/GICv4: ProcNum[9:0], DPGS[10], Last[11], DirectLPI[12],
 * CommonLPIAff[15:13], Aff2[23:16], Aff3[31:24], Aff1[39:32], Aff0[47:40]). */
#define GICR_TYPER_LAST                       (1ULL << 11)
#define GICR_TYPER_AFF0_SHIFT                 40
#define GICR_TYPER_AFF0_MASK                  (0xFFULL << GICR_TYPER_AFF0_SHIFT)
#define GICR_TYPER_AFF1_SHIFT                 32
#define GICR_TYPER_AFF1_MASK                  (0xFFULL << GICR_TYPER_AFF1_SHIFT)

/* SGI/PPI frame registers. */
#define GICR_IGROUPR0(i)                      (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0080)
#define GICR_ISENABLER0(i)                    (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0100)
#define GICR_ICENABLER0(i)                    (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0180)
#define GICR_ISPENDR0(i)                      (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0200)
#define GICR_ICPENDR0(i)                      (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0280)
#define GICR_ICACTIVER0(i)                    (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0380)
#define GICR_IPRIORITYR(i, n)                 (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0400 + (n) * 4)
#define GICR_IGROUPMOD0_ADDR(i)               (GICR_SGI_OFFSET + GICR_STRIDE * (i) + 0x0d00)

/* Priority mapping: liteos 0..31 -> GIC 8-bit field. */
#define GIC_PRIORITY_MASK                     0xFFU
#define GIC_PRIORITY_OFFSET                   8U

#ifndef LOSCFG_HWI_PRIO_LIMIT
#error "LOSCFG_HWI_PRIO_LIMIT is not defined!"
#endif

#if (LOSCFG_HWI_PRIO_LIMIT > 0) && (LOSCFG_HWI_PRIO_LIMIT <= 1)
#define MAX_BINARY_POINT_VALUE  7
#elif (LOSCFG_HWI_PRIO_LIMIT > 1) && (LOSCFG_HWI_PRIO_LIMIT <= 2)
#define MAX_BINARY_POINT_VALUE  6
#elif (LOSCFG_HWI_PRIO_LIMIT > 2) && (LOSCFG_HWI_PRIO_LIMIT <= 4)
#define MAX_BINARY_POINT_VALUE  5
#elif (LOSCFG_HWI_PRIO_LIMIT > 4) && (LOSCFG_HWI_PRIO_LIMIT <= 8)
#define MAX_BINARY_POINT_VALUE  4
#elif (LOSCFG_HWI_PRIO_LIMIT > 8) && (LOSCFG_HWI_PRIO_LIMIT <= 16)
#define MAX_BINARY_POINT_VALUE  3
#elif (LOSCFG_HWI_PRIO_LIMIT > 16) && (LOSCFG_HWI_PRIO_LIMIT <= 32)
#define MAX_BINARY_POINT_VALUE  2
#elif (LOSCFG_HWI_PRIO_LIMIT > 32) && (LOSCFG_HWI_PRIO_LIMIT <= 64)
#define MAX_BINARY_POINT_VALUE  1
#elif (LOSCFG_HWI_PRIO_LIMIT > 64) && (LOSCFG_HWI_PRIO_LIMIT <= 128)
#define MAX_BINARY_POINT_VALUE  0
#else
#error "LOSCFG_HWI_PRIO_LIMIT error configuration"
#endif

#define PRIORITY_SHIFT                        ((MAX_BINARY_POINT_VALUE + 1) % GIC_PRIORITY_OFFSET)

#define GIC_INT_PRI(liteosPrio)               ((UINT8)((liteosPrio) << PRIORITY_SHIFT))
#define GIC_INT_DEF_PRI                       GIC_INT_PRI(OS_HWI_PRIO_LOWEST)

/* MPIDR helpers. */
#define MPIDR_AFF_LEVEL0_SHIFT                0
#define MPIDR_AFF_LEVEL1_SHIFT                8
#define MPIDR_AFF_LEVEL_MASK                  0xFFULL
#define MPIDR_AFF_LEVEL(mpidr, level)         \
    (((mpidr) >> MPIDR_AFF_LEVEL##level##_SHIFT) & MPIDR_AFF_LEVEL_MASK)

/* ============================================================================
 * Interrupt lock/unlock (DAIF).
 * ArchIntLock/ArchIntUnlock/ArchIntRestore are declared in
 * los_interrupt.h and implemented in los_dispatch.S.
 * ==========================================================================*/
#define PSR_I_BIT                             0x00000080U
#define PSR_F_BIT                             0x00000040U

STATIC INLINE UINT32 ArchIntLocked(VOID)
{
    UINT64 intSave;
    __asm__ __volatile__("mrs %0, daif" : "=r"(intSave) : : "memory");
    return (UINT32)(intSave & PSR_I_BIT);
}

/* ============================================================================
 * Exception context and info (must match push_all in los_exc.S).
 * ==========================================================================*/
typedef struct TagExcContext {
    UINT64 X[30];       /* X0-X29 */
    UINT64 LR;          /* X30, offset 0xF0 */
    UINT64 uwReserved;  /* scratch, offset 0xF8 */
    UINT64 uwPC;        /* ELR_EL1, offset 0x100 */
    UINT64 uwxPSR;      /* SPSR_EL1, offset 0x108 */
} EXC_CONTEXT_S;

typedef VOID (*EXC_PROC_FUNC)(UINT32, EXC_CONTEXT_S *);

extern EXC_PROC_FUNC g_excRegHook;
extern UINT32 ArchSetExcHook(EXC_PROC_FUNC excHook);

VOID HalExcHandleEntry(UINT32 excType, UINT64 faultAddr, UINT32 pid, EXC_CONTEXT_S *excBufAddr);

/* Exception causes (liteos_m contract, see cortex_a_r). */
#define OS_EXC_CAUSE_UNDEF                     0
#define OS_EXC_CAUSE_PABT                      1
#define OS_EXC_CAUSE_DABT                      2
#define OS_EXC_CAUSE_UNUSED                    3
#define OS_EXC_CAUSE_NMI                       16
#define OS_EXC_CAUSE_HARDFAULT                 17
#define OS_EXC_CAUSE_TASK_EXIT                 18
#define OS_EXC_CAUSE_FATAL_ERR                 19

/* OS_EXC_FLAG_FAULTADDR_VALID / OS_EXC_FLAG_IN_HWI / OS_EXC_IN_INIT /
 * OS_EXC_IN_TASK / OS_EXC_IN_HWI / OS_EXC_IMPRECISE_ACCESS_ADDR /
 * OS_EXC_EVENT come from los_common_interrupt.h. */

#ifndef OS_EXC_MAX_BUF_LEN
#define OS_EXC_MAX_BUF_LEN                     25
#endif

#ifndef OS_EXC_MAX_NEST_DEPTH
#define OS_EXC_MAX_NEST_DEPTH                  1
#endif

/* No NVIC-style int-status dump on GICv3; exchook sizing only. */
#define MAX_INT_INFO_SIZE                      (0)

typedef struct TagExcInfo {
    UINT16 phase;
    UINT16 type;
    UINT64 faultAddr;
    UINT32 thrdPid;
    UINT16 nestCnt;
    UINT16 reserved;
    EXC_CONTEXT_S *context;
} ExcInfo;

extern ExcInfo g_excInfo;
extern UINT32 g_curNestCount;

extern VOID HalInterrupt(VOID);
extern VOID HalHwiInit(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _LOS_ARCH_INTERRUPT_H */
