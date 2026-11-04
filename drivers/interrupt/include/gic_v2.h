/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: GICv2 CPU Interface (GICC) register layout as a C struct.
 *              GICC_OFFSET must be defined by the board's soc.h before
 *              including this header (usually via gic_common.h → soc.h).
 * Reference: arm document IHI0048B/b — GIC Architecture Specification.
 */
#ifndef _GIC_V2_H
#define _GIC_V2_H

#include "gic_common.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* ---- CPU Interface (GICC) register struct ----
 * Maps the GICv2 CPU interface register bank up to the registers used
 * by this driver (ctlr, pmr, iar, eoir). Aliased and debug registers
 * beyond 0x028 are omitted — add fields if needed.
 *
 * GICC_OFFSET is SoC-specific (defined in soc.h).
 * Access via the GICC pointer, e.g.  GICC->iar;
 */
struct GicCpuInterface {
    volatile UINT32 ctlr;     /* 0x00 CPU Interface Control Register */
    volatile UINT32 pmr;      /* 0x04 Interrupt Priority Mask Register */
    volatile UINT32 bpr;      /* 0x08 Binary Point Register */
    volatile UINT32 iar;      /* 0x0C Interrupt Acknowledge Register */
    volatile UINT32 eoir;     /* 0x10 End of Interrupt Register */
    volatile UINT32 rpr;     /* 0x14 Running Priority Register */
    volatile UINT32 hppir;   /* 0x18 Highest Priority Pending Interrupt Register */
    volatile UINT32 abpr;     /* 0x1C Aliased Binary Point Register */
    volatile UINT32 aiar;     /* 0x20 Aliased Interrupt Acknowledge Register */
    volatile UINT32 aeoir;    /* 0x24 Aliased End of Interrupt Register */
    volatile UINT32 ahppir;   /* 0x28 Aliased Highest Priority Pending Interrupt Register */
};

#define GICC   ((volatile struct GicCpuInterface *)(UINTPTR)(GIC_BASE_ADDR + GICC_OFFSET))

/* IAR interrupt ID mask (bits 9:0 for standard GICv2) */
#define GICC_IAR_INTID_MASK             0x3FFU

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _GIC_V2_H */
