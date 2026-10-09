/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * SPDX-License-Identifier: MulanPSL-2.0
 */

/*
 * @file    liteos/irq.h
 * @brief   LiteOS-M IRQ backend for the libmica platform layer.
 *
 * RK3568 AMP slave core: the cross-core IPI is hardware SGI7 (GICv3 INTID 7;
 * master mcs_km maps it to Linux virq 8).
 * mica_request_irq registers the handler through the LiteOS-M GIC port
 * (ArchHwiCreate also enables the line for this redistributor).
 *
 * mica_trigger_irq kicks the master core (Linux on CPU0) with the same SGI
 * via ICC_SGI1R (Group1 NS), which is exactly how the master-side mcs_km
 * expects vring notifications: IntID in bits [27:24], Aff1 in [23:16],
 * TargetList in [15:0] (Aff0 bitmap of the addressed cluster).
 */

#ifndef __MICA_IRQ__H__
#error "Include mica/platform/irq.h instead of mica/platform/system/liteos/irq.h"
#endif

#ifndef __MICA_LITEOS_IRQ__H__
#define __MICA_LITEOS_IRQ__H__

#include <stdint.h>
#include "los_interrupt.h"
#include "los_arch_interrupt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef VOID (*mica_irq_handler_t)(VOID);

static inline int mica_request_irq(unsigned int irq, mica_irq_handler_t handler)
{
    int ret = (ArchHwiCreate((HWI_HANDLE_T)irq, 0, 0, (HWI_PROC_FUNC)handler,
                             (VOID *)0) == LOS_OK) ? 0 : -1;
    if (ret == 0) {
        /*
         * GICR_IGROUPR0 resets to 0 (Group0/Secure). The Linux master sends
         * the notification SGI as Group1 NS via ICC_SGI1R, and a Group0
         * INTID never raises IRQ for it. Force our SGI to Group1 NS.
         * (Same fix as UniProton RpmsgHwiInit, RK3568: GICR@0xFD460000,
         * per-core stride 0x20000, SGI_base = RD_base + 0x10000.)
         */
        uint64_t mpidr;
        uintptr_t sgi_base;
        volatile uint32_t *igroupr0;
        volatile uint32_t *igrpmodr0;

        __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
        /* Aff1 selects the redistributor: all RK3568 cores have Aff0=0 */
        sgi_base = 0xFD460000UL + (uintptr_t)((mpidr >> 8) & 0xFFU) * 0x20000UL + 0x10000UL;
        igroupr0 = (volatile uint32_t *)(sgi_base + 0x0080U); /* GICR_IGROUPR0 */
        igrpmodr0 = (volatile uint32_t *)(sgi_base + 0x0D00U); /* GICR_IGRPMODR0 */
        *igroupr0 |= (1U << irq);
        *igrpmodr0 &= ~(1U << irq);
        __asm__ volatile("dsb sy" ::: "memory");
    }
    return ret;
}

static inline void mica_unmask_irq(unsigned int irq)
{
    (VOID)irq; /* ArchHwiCreate keeps the line enabled on this core */
}

/*
 * Send the SGI to the master core (CPU0). Linux runs on CPU0-2 with the
 * mcs_km percpu IPI handler registered, so targeting CPU0 is sufficient.
 */
static inline void mica_trigger_irq(unsigned int irq)
{
    /*
     * ICC_SGI1R_EL1 layout (GICv3, matches Linux gic_send_sgi):
     * Aff3[55:48] RS[47:44] RM[40] Aff2[39:32] INTID[27:24]
     * Aff1[23:16] TargetList[15:0]. TargetList is the Aff0 bitmap of
     * the cluster selected by the Aff1 field. RK3568 exposes each A55
     * as its own cluster (Aff0=0, Aff1=core number, verified via
     * GICR_TYPER), so every single-core target is TargetList bit0 with
     * the core number in the Aff1 field. Master Linux CPU0 is cluster
     * Aff1=0: SGI = INTID<<24 | 0<<16 | 1. (The previous (1ULL << 16)
     * meant Aff1=1 with an empty TargetList and was silently dropped.)
     */
    uint64_t sgi = ((uint64_t)irq << 24) | (1ULL << 0);

    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("msr S3_0_C12_C11_5, %0" : : "r"(sgi) : "memory"); /* ICC_SGI1R_EL1 */
    __asm__ volatile("isb" ::: "memory");
}

#ifdef __cplusplus
}
#endif

#endif /* __MICA_LITEOS_IRQ__H__ */
