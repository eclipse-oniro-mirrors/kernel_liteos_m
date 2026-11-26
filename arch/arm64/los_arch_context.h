/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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
 * AArch64 task context.
 * TaskContext layout must match PUSH/POP sequences in los_dispatch.S.
 */

#ifndef _LOS_ARCH_CONTEXT_H
#define _LOS_ARCH_CONTEXT_H

#include "los_config.h"
#include "los_compiler.h"
#include "los_arch_interrupt.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* Stack pointer alignment: SPSP... AArch64 requires 16-byte SP alignment.
 * Defined in target_config.h (before los_config.h's guarded default). */

#define FP_REGS_NUM                            32
#define GEN_REGS_NUM                           30

/* Layout matches dispatch.S: FPU region (Q0..Q31 + FPCR/FPSR) first,
 * then X0-X29, LR, PC, SPSR, NZCV. */
typedef struct TagTskContext {
#ifdef LOSCFG_ARCH_FPU_ENABLE
    UINT128 Q[FP_REGS_NUM]; /* Q0-Q31 */
    UINT64 regFPCR;         /* FPCR */
    UINT64 regFPSR;         /* FPSR */
#endif
    UINT64 X[GEN_REGS_NUM]; /* X0-X29 */
    UINT64 LR;              /* X30 */
    UINT64 PC;
    UINT64 regSPSR;
    UINT64 NZCV;
} TaskContext;

/* Extract the frame pointer (X29) from a suspended task's saved context. */
STATIC INLINE UINTPTR ArchGetTaskFp(const VOID *stackPointer)
{
    return (UINTPTR)(((TaskContext *)stackPointer)->X[29]); /* X29: FP */
}

STATIC INLINE VOID *ArchCurrTaskGet(VOID)
{
    VOID *task;
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r"(task));
    return task;
}

STATIC INLINE VOID ArchCurrTaskSet(const VOID *val)
{
    __asm__ __volatile__("msr tpidr_el1, %0" ::"r"(val));
    __asm__ __volatile__("isb" ::: "memory");
}

STATIC INLINE UINT32 ArchCurrCpuid(VOID)
{
#ifdef LOSCFG_KERNEL_SMP
    UINT64 mpidr;
    UINT32 cpuId;
    __asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(mpidr));
    if (mpidr & MPIDR_MT_MASK) { /* MT bit */
        cpuId = (UINT32)MPIDR_AFF_LEVEL(mpidr, 1);
    } else {
        cpuId = (UINT32)MPIDR_AFF_LEVEL(mpidr, 0);
    }
    return cpuId;
#else
    return 0;
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _LOS_ARCH_CONTEXT_H */
