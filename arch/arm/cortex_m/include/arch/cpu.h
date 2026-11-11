/* ----------------------------------------------------------------------------
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2013-2022. All rights reserved.
 * Description: CPU Operations HeadFile
 * Author: Huawei LiteOS Team
 * Create: 2013-01-01
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

/**
 * @defgroup cpu
 * @ingroup kernel
 */

#ifndef _ARCH_CPU_H
#define _ARCH_CPU_H

#include "los_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define CPUID_BASE              0xE000ED00U    /* Main ID Register */
#define ARM_SYSREG_READ(addr)   (*(volatile UINT32 *)(uintptr_t)(addr))

#define SYS_MODE_DEFAULT_KERNEL 0

typedef struct {
    const UINT32 partNo;
    const CHAR *cpuName;
} CpuVendor;

extern const CHAR *ArchCpuInfo(VOID);
extern UINT32 ArchGetPc(VOID);

#define ArchGetLr() ({\
    UINTPTR _regLr; \
    __asm__ __volatile__("mov %0, lr" : "=r"(_regLr)); \
    _regLr; \
})

LITE_OS_SEC_ALW_INLINE STATIC INLINE UINT32 OsMainIDGet(VOID)
{
    return ARM_SYSREG_READ(CPUID_BASE);
}

LITE_OS_SEC_ALW_INLINE STATIC INLINE UINT32 ArchSPGet(VOID)
{
    UINT32 val;
    asm volatile("mov %0, sp" : "=r"(val));
    return val;
}

LITE_OS_SEC_ALW_INLINE STATIC INLINE UINT32 ArchGetSp(VOID)
{
    UINT32 regSp;

    __asm__ __volatile__("mov %0, sp\n" : "=r"(regSp));

    return regSp;
}

LITE_OS_SEC_ALW_INLINE STATIC INLINE UINTPTR ArchGetPsp(VOID)
{
    UINTPTR regPsp;

    __asm__ __volatile__("mrs %0, psp\n" : "=r"(regPsp));

    return regPsp;
}

LITE_OS_SEC_ALW_INLINE STATIC INLINE UINTPTR ArchGetMsp(VOID)
{
    UINTPTR regMsp;

    __asm__ __volatile__("mrs %0, msp\n" : "=r"(regMsp));

    return regMsp;
}

LITE_OS_SEC_ALW_INLINE STATIC INLINE UINT32 ArchGetIpsr(VOID)
{
    UINT32 regIpsr;

    __asm__ __volatile__("mrs %0, ipsr" : "=r"(regIpsr));

    return regIpsr;
}

LITE_OS_SEC_ALW_INLINE STATIC INLINE UINT32 ArchCurrCpuid(VOID)
{
    return 0;
}

STATIC INLINE UINT32 ArchGetControl(VOID)
{
    UINT32 regControl;
    __asm__ __volatile__("mrs %0, control" : "=r"(regControl));
    return regControl;
}

STATIC INLINE UINT32 ArchGetPrimask(VOID)
{
    UINT32 regPrimask;
    __asm__ __volatile__("mrs %0, primask" : "=r"(regPrimask));
    return regPrimask;
}
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ARCH_CPU_H */
