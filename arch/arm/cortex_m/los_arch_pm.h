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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LOS_ARCH_PM_H
#define _LOS_ARCH_PM_H

#ifndef __ASSEMBLER__
#include "los_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __ASSEMBLER__ */

#if defined(LOSCFG_KERNEL_RUNSTOP) || defined(LOSCFG_KERNEL_DEEPSLEEP_RETENTION)
#define OS_CPU_REG_NUM 11 /* R4-R11, PSP, LR, PRIMASK */
#define OS_REG_SLOT_BYTES 4 /* UINT32 slot */

/* Shared with runstop.S. */
#define OS_REG_OFF_R4       0
#define OS_REG_OFF_R5       (OS_REG_OFF_R4 + OS_REG_SLOT_BYTES)
#define OS_REG_OFF_R6       (OS_REG_OFF_R5 + OS_REG_SLOT_BYTES)
#define OS_REG_OFF_R7       (OS_REG_OFF_R6 + OS_REG_SLOT_BYTES)
#define OS_REG_OFF_R8       (OS_REG_OFF_R7 + OS_REG_SLOT_BYTES)
#define OS_REG_OFF_R9       (OS_REG_OFF_R8 + OS_REG_SLOT_BYTES)
#define OS_REG_OFF_R10      (OS_REG_OFF_R9 + OS_REG_SLOT_BYTES)
#define OS_REG_OFF_R11      (OS_REG_OFF_R10 + OS_REG_SLOT_BYTES)
#define OS_REG_OFF_PSP      (OS_REG_OFF_R11 + OS_REG_SLOT_BYTES)
#define OS_REG_OFF_LR       (OS_REG_OFF_PSP + OS_REG_SLOT_BYTES)
#define OS_REG_OFF_PRIMASK  (OS_REG_OFF_LR + OS_REG_SLOT_BYTES)

#ifndef __ASSEMBLER__
extern UINT32 g_saveCpuReg[OS_CPU_REG_NUM];
extern VOID OsSRSaveRegister(VOID);
extern VOID OsSRRestoreRegister(VOID);
/* Compile-time guard: the offset chain must exactly fill the array. */
typedef char osCpuRegLayoutContract[
    (OS_REG_OFF_PRIMASK + OS_REG_SLOT_BYTES == OS_CPU_REG_NUM * OS_REG_SLOT_BYTES) ? 1 : -1];
#endif
#endif

#ifndef __ASSEMBLER__
#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* __ASSEMBLER__ */

#endif /* _LOS_ARCH_PM_H */
