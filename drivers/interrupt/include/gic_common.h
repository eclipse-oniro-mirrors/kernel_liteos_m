/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: GIC Distributor (GICD) register layout as a C struct.
 *              SoC-agnostic: the board's soc.h must provide GIC_BASE_ADDR
 *              and GICD_OFFSET before this header is included.
 * Reference: arm document IHI0048B/b — GIC Architecture Specification.
 */
#ifndef _GIC_COMMON_H
#define _GIC_COMMON_H

#include "los_typedef.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* ---- Distributor (GICD) register struct ----
 * Maps the GICv2 distributor register bank. Reserved gaps are explicit
 * padding so the layout matches the hardware offset of every field.
 * The struct ends at SGIR (0xF00); SGI clear/set-pending and PIDR2 are
 * not used by this driver and omitted for compactness.
 *
 * GIC_BASE_ADDR and GICD_OFFSET are SoC-specific (defined in soc.h).
 * Access via the GICD pointer, e.g.  GICD->isenabler[n] = ...;
 */
struct GicDistributor {
    volatile UINT32 ctlr;           /* 0x000 Distributor Control Register */
    volatile UINT32 typer;          /* 0x004 Interrupt Controller Type Register */
    volatile UINT32 iidr;           /* 0x008 Distributor Implementer ID Register */
    UINT32 reserved0[29];           /* 0x00C–0x07F */
    volatile UINT32 igroupr[32];    /* 0x080–0x0FC Interrupt Group Registers */
    volatile UINT32 isenabler[32];  /* 0x100–0x17C Interrupt Set-Enable Registers */
    volatile UINT32 icenabler[32];  /* 0x180–0x1FC Interrupt Clear-Enable Registers */
    volatile UINT32 ispendr[32];    /* 0x200–0x27C Interrupt Set-Pending Registers */
    volatile UINT32 icpendr[32];    /* 0x280–0x2FC Interrupt Clear-Pending Registers */
    volatile UINT32 isactiver[32]; /* 0x300–0x37C Interrupt Set-Active Registers */
    volatile UINT32 icactiver[32];  /* 0x380–0x3FC Interrupt Clear-Active Registers */
    volatile UINT32 ipriorityr[256];/* 0x400–0x7FC Interrupt Priority Registers */
    volatile UINT32 itargetsr[256]; /* 0x800–0xBFC Interrupt Processor Targets Registers */
    volatile UINT32 icfgr[64];      /* 0xC00–0xCFC Interrupt Configuration Registers */
    UINT32 reserved1[128];          /* 0xD00–0xEFC */
    volatile UINT32 sgir;          /* 0xF00 Software Generated Interrupt Register */
};

#define GICD   ((volatile struct GicDistributor *)(UINTPTR)(GIC_BASE_ADDR + GICD_OFFSET))

/* ---- Default priority ----
 * 0xa0 originates from Linux irq-gic.c (GICD_INT_DEF_PRI). It is an
 * engineering convention, not the spec-defined lowest priority.
 *
 * GICv2 priority layout (ARM IHI0048B/b):
 *   - GICD_IPRIORITYRn: 8 bits per IRQ, but implemented width is
 *     IMPLEMENTATION DEFINED (5-8 bits). Minimum 5-bit impl uses
 *     bits [7:3]; bits [2:0] are RAZ/WI.
 *   - Higher value = lower priority (opposite of Cortex-M NVIC).
 *   - True lowest priority in a 5-bit impl is 0xF8 (11111 000b).
 *
 * Why 0xa0 (not 0xF8): Linux commit 08246c3fbe7c reserved 0x00-0x1F
 * for Secure/FIQ high-priority interrupts; 0xa0 serves as the
 * Non-secure default, leaving 0xFF for PMR-maskable lowest priority.
 * For single-world RTOS (no Secure world) like LiteOS-M, 0xa0 is kept
 * purely for alignment with the Linux-derived GIC driver convention.
 *
 * _X4 packs 4 IRQ priorities into one 32-bit ipriorityr register
 * write (4 IRQs per 32-bit register), 4x faster than byte writes. */
#define GICD_INT_DEF_PRI                0xa0U
#define GICD_INT_DEF_PRI_X4             (((UINT32)GICD_INT_DEF_PRI << 24) | \
                                         ((UINT32)GICD_INT_DEF_PRI << 16) | \
                                         ((UINT32)GICD_INT_DEF_PRI << 8)  | \
                                         (UINT32)GICD_INT_DEF_PRI)

/* ---- Shared constants ----
 * GIC interrupt ID space: SGIs 0-15, PPIs 16-31, SPIs 32+.
 */
#ifndef GIC_MIN_PPI_NUM
#define GIC_MIN_PPI_NUM                 16
#endif

#ifndef GIC_MIN_SPI_NUM
#define GIC_MIN_SPI_NUM                 32
#endif

#ifndef TARGET_CPU_MASK
#define TARGET_CPU_MASK                 0x01010101U /* route SPIs to CPU 0 by default */
#endif

#define INT_COUNTS_4_PER_REG            4U   /* 4 interrupts per priority/target register */
#define INT_COUNTS_32_PER_REG           32U  /* 32 interrupts per enable/pending register */

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _GIC_COMMON_H */
