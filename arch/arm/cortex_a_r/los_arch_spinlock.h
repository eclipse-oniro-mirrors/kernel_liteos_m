/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifndef _LOS_ARCH_SPINLOCK_H
#define _LOS_ARCH_SPINLOCK_H

#include "los_compiler.h"

#ifdef LOSCFG_KERNEL_SMP

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


STATIC INLINE VOID ArchSpinLock(UINT32 *lock)
{
    UINT32 val;
    __asm__ __volatile__(
        "1: ldrex   %0, [%1]        \n"
        "   cmp     %0, #0          \n"
        "   wfene                   \n"
        "   strexeq %0, %2, [%1]    \n"
        "   cmpeq   %0, #0          \n"
        "   bne     1b              \n"
        "   dmb                     \n"
        : "=&r"(val)
        : "r"(lock), "r"(0x1)
        : "cc", "memory");
}

/*
 * when spinlock already taken, r1 will read 1 which is same with the
 * 'strex' failure return value, no extra conversion needed.
 */
STATIC INLINE INT32 ArchSpinTrylock(UINT32 *lock)
{
    INT32 val;
    UINT32 take = 0;
    __asm__ __volatile__(
        "   ldrex   %0, [%1]        \n"
        "   teq     %0, #0          \n"
        "   moveq   %2, #1          \n"
        "   strexeq %0, %2, [%1]    \n"
        "   dmb                     \n"
        : "=&r"(val), "+r"(take)
        : "r"(lock)
        : "cc", "memory");
    return val;
}

STATIC INLINE VOID ArchSpinUnlock(UINT32 *lock)
{
    __asm__ __volatile__(
        "   dmb                     \n"
        "   str     %1, [%0]        \n"
        "   dsb                     \n"
        "   sev                     \n"
        :
        : "r"(lock), "r"(0x0)
        : "memory");
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LOSCFG_KERNEL_SMP */

#endif /* _LOS_ARCH_SPINLOCK_H */
