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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "los_arch_pm.h"

.syntax unified
#ifdef LOSCFG_ARCH_ARM_V6M
.arch armv6-m
#else
.arch armv7e-m
.fpu fpv4-sp-d16
#endif
.thumb

/* g_saveCpuReg: SR context save area, layout in los_arch_pm.h. */
.bss
.align  2
.global g_saveCpuReg
g_saveCpuReg:
    .space OS_CPU_REG_NUM * OS_REG_SLOT_BYTES

.section .text

/* void OsSRSaveRegister(void)
 * Called from WowWriteFlashTask (PSP thread mode, PRIMASK=1). Leaf: no
 * stack usage, only r0/r1 scratched (both dead across the BL per AAPCS). */
.thumb_func
.type OsSRSaveRegister, %function
.global OsSRSaveRegister
OsSRSaveRegister:
    ldr     r0, =g_saveCpuReg
    /* R4-R7: directly storable (low registers) */
    str     r4, [r0, #OS_REG_OFF_R4]
    str     r5, [r0, #OS_REG_OFF_R5]
    str     r6, [r0, #OS_REG_OFF_R6]
    str     r7, [r0, #OS_REG_OFF_R7]
    /* R8-R11: no high-register STR on ARMv6-M, go through a low temp */
    mov     r1, r8
    str     r1, [r0, #OS_REG_OFF_R8]
    mov     r1, r9
    str     r1, [r0, #OS_REG_OFF_R9]
    mov     r1, r10
    str     r1, [r0, #OS_REG_OFF_R10]
    mov     r1, r11
    str     r1, [r0, #OS_REG_OFF_R11]
    /* SP / resume address / interrupt state */
    mrs     r1, psp
    str     r1, [r0, #OS_REG_OFF_PSP]
    mov     r1, lr
    str     r1, [r0, #OS_REG_OFF_LR]
    mrs     r1, primask
    str     r1, [r0, #OS_REG_OFF_PRIMASK]
    bx      lr

/* void OsSRRestoreRegister(void) - noreturn
 * Called on MSP from the cold-boot wake path (interrupts locked, RAM image
 * restored): switches to the suspended thread context and resumes; the
 * caller's MSP frame is abandoned on purpose. */
.thumb_func
.type OsSRRestoreRegister, %function
.global OsSRRestoreRegister
OsSRRestoreRegister:
    ldr     r0, =g_saveCpuReg
    ldr     r2, [r0, #OS_REG_OFF_LR]       /* resume address */
    ldr     r1, [r0, #OS_REG_OFF_PSP]      /* saved PSP */
    ldr     r3, [r0, #OS_REG_OFF_PRIMASK]  /* saved PRIMASK (1) */
    /* Set the target SP first (still on MSP, register r1 is untouched) */
    msr     psp, r1
    /* Privileged thread mode on PSP, same as ArchStartToRun */
    movs    r1, #2
    msr     control, r1
    isb
    /* R8-R11 via low temps (no high-register LDR on ARMv6-M) */
    ldr     r1, [r0, #OS_REG_OFF_R8]
    mov     r8, r1
    ldr     r1, [r0, #OS_REG_OFF_R9]
    mov     r9, r1
    ldr     r1, [r0, #OS_REG_OFF_R10]
    mov     r10, r1
    ldr     r1, [r0, #OS_REG_OFF_R11]
    mov     r11, r1
    /* R4-R7 */
    ldr     r4, [r0, #OS_REG_OFF_R4]
    ldr     r5, [r0, #OS_REG_OFF_R5]
    ldr     r6, [r0, #OS_REG_OFF_R6]
    ldr     r7, [r0, #OS_REG_OFF_R7]
    /* Restore interrupt state, then jump (direct branch, not EXC_RETURN) */
    msr     primask, r3
    mov     lr, r2
    bx      lr

.size OsSRSaveRegister, . - OsSRSaveRegister
.size OsSRRestoreRegister, . - OsSRRestoreRegister
