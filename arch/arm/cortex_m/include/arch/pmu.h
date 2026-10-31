/* ----------------------------------------------------------------------------
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022-2022. All rights reserved.
 * Description: LiteOS Perf Armv8m Pmu Module HeadFile
 * Author: Huawei LiteOS Team
 * Create: 2022-06-09
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --------------------------------------------------------------------------- */

#ifndef _ARCH_PMU_H
#define _ARCH_PMU_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Performance Monitoring Unit Register */
#define EVCNTR_BASE          0xE0003000UL
#define PMU_EVCNTRn(n)       (EVCNTR_BASE + 4 * (n))     /* Cycle Counter Filter Register */

#define PMU_CCNTR            0xE000307CUL                /* Cycle Counter Filter Register */

#define EVTYPER_BASE         0xE0003400UL
#define PMU_EVTYPERn(n)      (EVTYPER_BASE + 4 * (n))    /* Event Type and Filter Register */
#define EVENT_TYPE_MASK      0xFFFF                      /* Mask for writable bits */

#define PMU_CCFILTR          0xE000347CUL                /* Cycle Counter Filter Register */

#define PMU_CNTENCLR         0xE0003C20UL                /* Count Enable Clear Register */
#define PMU_CNTENSET         0xE0003C00UL                /* Count Enable Set Register */

#define PMU_INTENCLR         0xE0003C60UL                /* Interrupt Enable Clear Register */
#define PMU_INTENSET         0xE0003C40UL                /* Interrupt Enable Set Register */

#define PMU_OVSCLR           0xE0003C80UL                /* Overflow Flag Status Clear Register */
#define PMU_OVSSET           0xE0003CC0UL                /* Overflow Flag Status Set Register */

#define PMU_CTRL             0xE0003E04UL                /* Control Register */
#define CTRL_E_BIT           (1U << 0)
#define CTRL_P_BIT           (1U << 1)
#define CTRL_C_BIT           (1U << 2)
#define CTRL_FZO_BIT         (1U << 9)

#define PMU_TYPE             0xE0003E00UL                /* Type Register */
#define TYPE_N_MASK          0xFF

#define PMU_SWINC            0xE0003CA0UL                /* Software Increment Register */

/* Data Watchpoint and Trace Register */
#define DWT_CTRL             0xE0001000UL
#define FOLDEVTENA_MASK      (1U << 21)
#define LSUEVTENA_MASK       (1U << 20)
#define SLEEPEVTENA_MASK     (1U << 19)
#define EXCEVTENA_MASK       (1U << 18)
#define CPIEVTENA_MASK       (1U << 17)
#define DWT_ENABLE_MASK      (FOLDEVTENA_MASK | LSUEVTENA_MASK | SLEEPEVTENA_MASK | EXCEVTENA_MASK | CPIEVTENA_MASK)

/* Debug Control Block Register */
#define DEMCR_REG               0xE000EDFCUL             /* Debug Exception and Monitor Control Register */
#define DEMCR_UMON_EN_MASK      (1U << 21)               /* Unprivileged monitor enable */
#define DEMCR_MON_EN_MASK       (1U << 16)               /* Monitor enable */
#define DEMCR_MON_PEND_MASK     (1U << 17)               /* Monitor pend */
#define DEMCR_MON_TRCENA_MASK   (1U << 24)               /* Trace enable */

/* counters index */
#define PMU_IDX_COUNTER0        1
#define PMU_IDX_CYCLE_COUNTER   0
#define PMU_MAX_COUNTERS        32
#define PMU_COUNTER_MASK        (PMU_MAX_COUNTERS - 1)

/* arm event counter index mapping */
#define PMU_CNT2BIT(x)          (1UL << (x))
#define PMU_IDX2CNT(x)          (((x) - PMU_IDX_COUNTER0) & PMU_COUNTER_MASK)

/* pmu event type */
#define EVENT_CYCLES            0xFFFF /* cycles */
#define EVENT_SW_INCR           0x0000 /* software increment */
#define EVENT_L1I_CACHE_REFILL  0x0001 /* Attributable Level 1 instruction cache refill */
#define EVENT_L1D_CACHE_REFILL  0x0003 /* Attributable Level 1 data cache refill */
#define EVENT_L1D_CACHE         0x0004 /* Attributable Level 1 data cache access */
#define EVENT_INST_RETIRED      0x0008 /* Instruction Architecturally executed */
#define EVENT_PC_WRITE_RETIRED  0x000C /* software change of the PC */
#define EVENT_BR_MIS_PRED       0x0010 /* Mispredicted or not predicted branch speculatively executed */
#define EVENT_L1I_CACHE         0x0014 /* Attributable Level 1 instruction cache access */

VOID OsPmuIrqHandler(VOID);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ARCH_PMU_H */