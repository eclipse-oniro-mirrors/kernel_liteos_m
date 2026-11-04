/*
 * Copyright (c) 2020-2022 Huawei Device Co., Ltd. All rights reserved.
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
 */

#ifndef _LOS_ARCH_CONTEXT_H
#define _LOS_ARCH_CONTEXT_H

#include "los_config.h"
#include "los_compiler.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#define LOSCFG_STACK_POINT_ALIGN_SIZE 8

#if defined(LOSCFG_ARCH_FPU_VFP_D16)
#define FP_REGS_NUM     16
#elif defined(LOSCFG_ARCH_FPU_VFP_D32)
#define FP_REGS_NUM     32
#endif
#define GEN_REGS_NUM    13

typedef struct TagTskContext {
#ifdef LOSCFG_ARCH_FPU_ENABLE
    UINT64 D[FP_REGS_NUM];  /* D0-D31 */
    UINT32 regFPSCR;        /* FPSCR */
    UINT32 regFPEXC;        /* FPEXC */
#endif
    UINT32 R[GEN_REGS_NUM]; /* R0-R12 */
    UINT32 LR;              /* R14 */
    UINT32 PC;              /* R15 */
    UINT32 regPSR;
} TaskContext;

STATIC INLINE UINTPTR ArchGetTaskFp(const VOID *stackPointer)
{
    return (UINTPTR)(((TaskContext *)stackPointer)->R[11]); /* R11: FP */
}

STATIC INLINE VOID *ArchCurrTaskGet(VOID)
{
    VOID *task;
    __asm__ __volatile__("mrc p15, 0, %0, c13, c0, 4" : "=r"(task));
    return task;
}

STATIC INLINE VOID ArchCurrTaskSet(VOID *val)
{
    __asm__ __volatile__("mcr p15, 0, %0, c13, c0, 4" :: "r"(val));
    __asm__ __volatile__("isb" ::: "memory");
}

STATIC INLINE UINT32 ArchCurrCpuid(VOID)
{
#ifdef LOSCFG_KERNEL_SMP
    UINT32 cpuid;
    __asm__ __volatile__("mrc p15, 0, %0, c0, c0, 5" : "=r"(cpuid));
    return cpuid & 0x03;
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
