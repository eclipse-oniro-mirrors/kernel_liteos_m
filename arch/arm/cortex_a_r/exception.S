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

.extern HalExcHandleEntry
.extern main
.extern OsMmuInit
.extern g_percpuNestCount
.extern __bss_start
.extern __bss_end

#include "include/arch/asm.h"

.equ OS_EXCEPT_RESET,           0x00
.equ OS_EXCEPT_UNDEF_INSTR,     0x01
.equ OS_EXCEPT_SWI,             0x02
.equ OS_EXCEPT_PREFETCH_ABORT,  0x03
.equ OS_EXCEPT_DATA_ABORT,      0x04
.equ OS_EXCEPT_FIQ,             0x05
.equ OS_EXCEPT_ADDR_ABORT,      0x06
.equ OS_EXCEPT_IRQ,             0x07

.macro STACK_GUARD base_sym, top_sym, size
    ldr     r0, =\base_sym
    str     r2, [r0]
    ldr     r0, =\top_sym
    sub     r0, r0, #\size
    str     r2, [r0]
.endm


    .section .vectors, "ax"
    .align 5
    .global __exception_handlers
    .global reset_vector
    .global _start
_start:
__exception_handlers:
    b   reset_vector               /* 0x00: reset */
    b   HalExcUndefInstr           /* 0x04: undefined instruction */
    b   HalExcSvcCall              /* 0x08: SVC */
    b   HalExcPrefAbort            /* 0x0C: prefetch abort */
    b   HalExcDataAbort            /* 0x10: data abort */
    b   .                         /* 0x14: not used */
    b   OsIrqHandler               /* 0x18: IRQ */
    b   HalExcFiq                  /* 0x1C: FIQ */

reset_vector:

    mov     r0, #0
    mcr     p15, 0, r0, c13, c0, 4   /* clear TPIDRPRW (percpu) */
    mrc     p15, 0, r0, c1, c0, 0    /* read SCTLR */
    bic     r0, r0, #(1 << 12)      /* ICache off */
    bic     r0, r0, #(1 << 2)       /* DCache off */
    bic     r0, r0, #(1 << 0)       /* MMU off */
    mcr     p15, 0, r0, c1, c0, 0   /* write SCTLR */
    isb

    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0   /* invalidate ICache (all) */
    mcr     p15, 0, r0, c7, c5, 6   /* invalidate BTB */
    mcr     p15, 0, r0, c8, c7, 0   /* invalidate TLB (Unified) */
    dsb
    isb

    ldr     r0, =__exception_handlers
    mcr     p15, 0, r0, c12, c0, 0  /* VBAR */
    isb

    mrc     p15, 0, r0, c0, c0, 5   /* MPIDR */
    and     r0, r0, #0x03
    cmp     r0, #1
    beq     core1_entry

    msr     cpsr_c, #0xD2              /* IRQ mode */
    ldr     sp, =__irq_stack_top
    msr     cpsr_c, #0xDB              /* UND mode */
    ldr     sp, =__und_stack_top
    msr     cpsr_c, #0xD7              /* ABT mode */
    ldr     sp, =__abt_stack_top
    msr     cpsr_c, #0xD1              /* FIQ mode */
    ldr     sp, =__fiq_stack_top
    msr     cpsr_c, #0xD3              /* SVC mode, IRQ/FIQ off */
    ldr     sp, =__svc_stack_top

    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    ldr     r0, =__svc_stack
    ldr     r1, =__und_stack_top
    ldr     r2, =0xCCCCCCCC
2:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     2b
    STACK_GUARD __svc_stack, __svc_stack_top, 0x4000
    STACK_GUARD __exc_stack, __exc_stack_top, 0x1000
    STACK_GUARD __irq_stack, __irq_stack_top, 0x400
    STACK_GUARD __fiq_stack, __fiq_stack_top, 0x400
    STACK_GUARD __abt_stack, __abt_stack_top, 0x400
    STACK_GUARD __und_stack, __und_stack_top, 0x400


    blx     OsMmuInit

    blx     main

hang:
    b       hang

    .global core1_entry
core1_entry:
    msr     cpsr_c, #0xD3

    EXC_SP_SET __svc_stack_top, 0x4000, r0, r1
    msr     cpsr_c, #0xD2
    EXC_SP_SET __irq_stack_top, 0x400, r0, r1
    msr     cpsr_c, #0xD1
    EXC_SP_SET __fiq_stack_top, 0x400, r0, r1
    msr     cpsr_c, #0xD7
    EXC_SP_SET __abt_stack_top, 0x400, r0, r1
    msr     cpsr_c, #0xDB
    EXC_SP_SET __und_stack_top, 0x400, r0, r1
    msr     cpsr_c, #0xD3

    bl      HalSecondaryCpuStart

core1_hang:
    wfi
    b       core1_hang

    .section .text
    .align 5

    .global HalExcUndefInstr
    .type   HalExcUndefInstr, %function
HalExcUndefInstr:
    STMFD   SP, {R0-R7}
    mov     r0, #OS_EXCEPT_UNDEF_INSTR
    mov     r4, lr
    b       HalExcDispatch

HalExcPrefAbort:
    sub     lr, lr, #4
    STMFD   SP, {R0-R7}
    mov     r0, #OS_EXCEPT_PREFETCH_ABORT
    mov     r4, lr
    b       HalExcDispatch

HalExcDataAbort:
    sub     lr, lr, #8
    STMFD   SP, {R0-R7}
    mov     r0, #OS_EXCEPT_DATA_ABORT
    mov     r4, lr
    b       HalExcDispatch

HalExcSvcCall:

    movs    pc, lr

    .global HalExcFiq
    .type   HalExcFiq, %function
HalExcFiq:
    sub     lr, lr, #4
    STMFD   SP, {R0-R7}
    mov     r0, #OS_EXCEPT_FIQ
    mov     r4, lr
    b       HalExcDispatch

HalExcDispatch:
    mrs     r1, spsr               /* pre-exception CPSR */
    mov     r2, lr                 /* faulting PC */
    sub     r3, sp, #(8 * 4)       /* saved R0-R7 */
    msr     cpsr_c, #0xD3          /* SVC mode, IRQ/FIQ off */
    mov     r5, sp                 /* task SP */
    EXC_SP_SET __exc_stack_top, 0x1000, r6, r7
    STMFD   SP!, {R4}              /* faultAddr, keeps 8-byte align */
    STMFD   SP!, {R1}              /* uwxPSR */
    STMFD   SP!, {R2}              /* uwPC */
    STMFD   SP!, {LR}              /* uwLR */
    STMFD   SP!, {R5}              /* uwSP */
    STMFD   SP!, {R8-R12}
    LDMFD   R3!, {R4-R11}          /* original R0-R7 */
    STMFD   SP!, {R4-R11}
    mov     r6, sp                 /* r6 = ExcContext */
    ldr     r7, =g_percpuNestCount
    mrc     p15, 0, r1, c0, c0, 5
    and     r1, r1, #0x03          /* get cpu id */
    ldr     r7, [r7, r1, lsl #2]
    cmp     r7, #0
    bne     HalExcCall             /* nested: run C on exc stack */
    EXC_SP_SET __svc_stack_top, 0x4000, r1, r12
HalExcCall:
    mov     r3, r6
    mov     r2, #0                 /* pid */
    ldr     r1, [r6, #68]          /* faultAddr */
    blx     HalExcHandleEntry
    b       .
