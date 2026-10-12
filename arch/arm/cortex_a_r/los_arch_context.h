/*
 * Copyright (c) 2024 morpheus
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

/* Extract the frame pointer (R11, ARM EABI fp) from a suspended task's saved context. */
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
