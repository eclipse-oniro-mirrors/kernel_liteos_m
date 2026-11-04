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

#ifndef _LOS_ARCH_INTERRUPT_H
#define _LOS_ARCH_INTERRUPT_H

#include "los_common_interrupt.h"
#include "soc.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#ifndef OS_HWI_PRIO_HIGHEST
#define OS_HWI_PRIO_HIGHEST                   0
#endif

#ifndef OS_HWI_PRIO_LOWEST
#define OS_HWI_PRIO_LOWEST                    7
#endif

#define OS_SYS_VECTOR_CNT                     0
#define OS_VECTOR_CNT                         (OS_SYS_VECTOR_CNT + OS_HWI_MAX_NUM)

typedef INT32 IRQn_Type;

#define PSR_MODE_SVC_ARM   (0x13U | 0xC0U) /* SVC mode | I | F */


extern VOID HalIrqInitPercpu(VOID);

#define OS_GIC_TIMER_IRQ                       29

extern VOID HalInterrupt(VOID);
extern VOID HalHwiInit(VOID);

#define OS_EXC_FLAG_NO_FLOAT                   0x10000000

typedef struct TagExcContext {
    UINT32 uwR0;
    UINT32 uwR1;
    UINT32 uwR2;
    UINT32 uwR3;
    UINT32 uwR4;
    UINT32 uwR5;
    UINT32 uwR6;
    UINT32 uwR7;
    UINT32 uwR8;
    UINT32 uwR9;
    UINT32 uwR10;
    UINT32 uwR11;
    UINT32 uwR12;
    UINT32 uwSP;
    UINT32 uwLR;
    UINT32 uwPC;
    UINT32 uwxPSR;
} EXC_CONTEXT_S;

typedef VOID (*EXC_PROC_FUNC)(UINT32, EXC_CONTEXT_S *);

extern EXC_PROC_FUNC g_excRegHook;
extern UINT32 ArchSetExcHook(EXC_PROC_FUNC excHook);

VOID HalExcHandleEntry(UINT32 excType, UINT32 faultAddr, UINT32 pid, EXC_CONTEXT_S *excBufAddr);

#define OS_EXC_CAUSE_UNDEF                     0
#define OS_EXC_CAUSE_PABT                      1
#define OS_EXC_CAUSE_DABT                      2
#define OS_EXC_CAUSE_UNUSED                    3
#define OS_EXC_CAUSE_NMI                       16
#define OS_EXC_CAUSE_HARDFAULT                17
#define OS_EXC_CAUSE_TASK_EXIT                18
#define OS_EXC_CAUSE_FATAL_ERR                19

#define OS_EXC_FLAG_FAULTADDR_VALID            0x01
#define OS_EXC_FLAG_IN_HWI                     0x02
#define OS_EXC_IMPRECISE_ACCESS_ADDR           0xABABABAB
#define OS_EXC_EVENT                           0x00000001

#define OS_EXC_MAX_BUF_LEN                     25
#define OS_EXC_MAX_NEST_DEPTH                   1

typedef struct TagExcInfo {
    UINT16 phase;
    UINT16 type;
    UINT32 faultAddr;
    UINT32 thrdPid;
    UINT16 nestCnt;
    UINT16 reserved;
    EXC_CONTEXT_S *context;
} ExcInfo;

extern ExcInfo g_excInfo;

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _LOS_ARCH_INTERRUPT_H */
