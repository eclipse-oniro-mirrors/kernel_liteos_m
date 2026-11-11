/* ----------------------------------------------------------------------------
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022-2022. All rights reserved.
 * Description: LiteOS Arch Perf HeadFile
 * Author: Huawei LiteOS Team
 * Create: 2022-07-05
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

#ifndef _ARCH_PERF_H
#define _ARCH_PERF_H

#include "los_typedef.h"
#include "arch/exception.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define IRQ_CONTEXT_PC_OFFSET       24
#define CCNT_CYCLE_FULL             0xFFFFFFFF
#define CYCLE_PERIOD_LOWER_BOUND    0x00000000
#define CYCLE_PERIOD_UPPER_BOUND    0xFFFFFF00
#define PERIOD_CYCLE_CALC(p)       ((CCNT_CYCLE_FULL - (p)) + 1)
#define VALID_CYCLE_PERIOD(p)       ((PERIOD_CYCLE_CALC(p) > CYCLE_PERIOD_LOWER_BOUND) \
                                    && (PERIOD_CYCLE_CALC(p) < CYCLE_PERIOD_UPPER_BOUND))

#define CCNT_EVENT_FULL             0xFFFF
#define EVENT_PERIOD_LOWER_BOUND    0x0000
#define EVENT_PERIOD_UPPER_BOUND    0xFF00
#define PERIOD_EVENT_CALC(p)        ((CCNT_EVENT_FULL - (p)) + 1)
#define VALID_EVENT_PERIOD(p)       ((PERIOD_EVENT_CALC(p) > EVENT_PERIOD_LOWER_BOUND) \
                                    && (PERIOD_EVENT_CALC(p) < EVENT_PERIOD_UPPER_BOUND))

#ifndef LOSCFG_COMPILER_ICCARM
#define OsPerfArchFetchCallerRegs(regs)                    \
    do {                                                   \
        (regs)->pc = (UINTPTR)__builtin_return_address(0); \
        (regs)->fp = (UINTPTR)__builtin_frame_address(0);  \
    } while (0)
#endif

#define OsPerfArchFetchIrqRegs(regs, tcb)                             \
    do {                                                              \
        (VOID)(tcb);                                                  \
        __asm__ __volatile__("MRS %0, PSP" : "=r"((regs)->fp));       \
        (regs)->pc = *(UINT32 *)((regs)->fp + IRQ_CONTEXT_PC_OFFSET); \
    } while (0)

STATIC INLINE UINT32 ArchPerfBackTraceGet(UINTPTR fp, UINTPTR pc, UINTPTR *callChain, UINT32 maxDepth)
{
    (VOID)pc;
    return ArchBackTraceGet(fp, callChain, maxDepth, 0);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ARCH_PERF_H */
