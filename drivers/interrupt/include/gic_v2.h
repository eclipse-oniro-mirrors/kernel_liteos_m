/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
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
