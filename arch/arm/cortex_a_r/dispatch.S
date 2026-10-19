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

.syntax unified
.arch armv7-a
.arm

.extern OsTaskEntry
.extern g_curIrqNum
.extern HalInterrupt
.extern OsSchedProcSchedFlag

#include "include/arch/asm.h"    

.equ OS_PSR_INT_DIS,    0xC0U
.equ OS_PSR_MODE_SVC,   0x13U

.macro STACK_ALIGN, reg
    MOV     \reg, sp
    TST     SP, #4
    SUBEQ   SP, #4
    PUSH    { \reg }
.endm

.macro STACK_RESTORE, reg
    POP     { \reg }
    MOV     sp, \reg
.endm

.macro PUSH_FPU_REGS reg1
#ifdef LOSCFG_ARCH_FPU_ENABLE
    VMRS    \reg1, FPEXC
    PUSH    {\reg1}
    VMRS    \reg1, FPSCR
    PUSH    {\reg1}
#ifdef LOSCFG_ARCH_FPU_VFP_D32
    VPUSH   {D16-D31}
#endif
    VPUSH   {D0-D15}
#endif
.endm

.macro POP_FPU_REGS reg1
#ifdef LOSCFG_ARCH_FPU_ENABLE
    VPOP    {D0-D15}
#ifdef LOSCFG_ARCH_FPU_VFP_D32
    VPOP    {D16-D31}
#endif
    POP     {\reg1}
    VMSR    FPSCR, \reg1
    POP     {\reg1}
    VMSR    FPEXC, \reg1
#endif
.endm

    .global ArchIntLock
    .type   ArchIntLock, %function
ArchIntLock:
    mrs     r0, cpsr
    cpsid   if
    bx      lr

    .global ArchIntUnLock
    .type   ArchIntUnLock, %function
ArchIntUnLock:
    mrs     r0, cpsr
    cpsie   if
    bx      lr

    .global ArchIntRestore
    .type   ArchIntRestore, %function
ArchIntRestore:
    msr     cpsr_c, r0
    bx      lr

    .global ArchDeferSchedule
    .type   ArchDeferSchedule, %function
ArchDeferSchedule:
    bx      lr

    .global ArchTaskSchedule
    .type   ArchTaskSchedule, %function
ArchTaskSchedule:
    MRS     R2, CPSR
    PUSH    {R2}
    PUSH    {LR}
    PUSH    {R0-R12, LR}
    PUSH_FPU_REGS R2

    STR     SP, [R1]            /* runTask->stackPointer = sp */

OsTaskContextLoad:
    CLREX

    LDR     SP, [R0]            /* sp = newTask->stackPointer */

    POP_FPU_REGS R2
    POP     {R0-R12, LR}        /* R0-R12 + LR */
    POP     {R1}
    POP     {R2}
    MSR     CPSR, R2
    BX      R1
    nop

    .global ArchStartToRun
    .type   ArchStartToRun, %function
ArchStartToRun:
    MSR     CPSR_c, #(OS_PSR_INT_DIS | OS_PSR_MODE_SVC)  /* SVC + IRQ/FIQ off */

    MCR     p15, 0, R0, c13, c0, 4
    ISB

#ifdef LOSCFG_ARCH_FPU_ENABLE

    VPUSH   {S0}
    VPOP    {S0}
    VPUSH   {D0}
    VPOP    {D0}
#endif

    B       OsTaskContextLoad

    .global OsIrqHandler
    .type   OsIrqHandler, %function
OsIrqHandler:
    SUB     LR, LR, #4              /* task PC */

    SRSDB   #0x13!

    CPSID   i, #0x13

    PUSH    {R0-R3, R12, LR}

    STACK_ALIGN     R0

    PUSH_FPU_REGS   R0

#ifdef LOSCFG_IRQ_USE_STANDALONE_STACK

#ifdef LOSCFG_ARCH_INTERRUPT_PREEMPTION
    BL      OsIrqNestingCntGet
    CMP     R0, #0
    BNE     OsIrqIntEntry
#endif

    PUSH    {R4}
    MOV     R4, SP
    EXC_SP_SET __svc_stack_top, OS_EXC_SVC_STACK_SIZE, R1, R2

OsIrqIntEntry:
#endif

    BLX     HalInterrupt

#ifdef LOSCFG_ARCH_INTERRUPT_PREEMPTION
    BL      OsIrqNestingCntGet
    CMP     R0, #0
    BNE     OsIrqExit
#endif

#ifdef LOSCFG_IRQ_USE_STANDALONE_STACK
    MOV     SP, R4
    POP     {R4}
#endif

    BL      OsSchedProcSchedFlag

OsIrqExit:
    POP_FPU_REGS    R0

    STACK_RESTORE   R0

OsIrqContextRestore:
    POP     {R0-R3, R12, LR}
    RFEIA   SP!

    .size OsIrqHandler, .-OsIrqHandler
