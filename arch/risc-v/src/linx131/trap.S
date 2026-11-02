/* ----------------------------------------------------------------------------
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022-2023. All rights reserved.
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: LinxCore131-specific trap (interrupt) entry.
 *              Replaces the default los_trap.S via BUILD.gn file replacement
 *              when LOSCFG_ARCH_LINXCORE_131 is set.
 *
 *              Key 131 difference vs standard RISC-V: hardware interrupt
 *              nesting via CSR prithd (0xBFE) + cipri (0x7ED). The standard
 *              trap uses mnxti; LinxCore131 has no mnxti and instead writes
 *              cipri to prithd so only higher-priority interrupts nest.
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list
 *    of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
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

#ifndef _LOS_TRAP_S
#define _LOS_TRAP_S
#include "arch/stack_ops.h"
/* Interrupt numbers from board/include/asm/soc.h — defined here as .equ
 * because soc.h has C declarations the assembler cannot parse. */
.equ NUM_HAL_INTERRUPT_NMI,   12
.equ NUM_HAL_INTERRUPT_TIMER, 26

/* Restore mepc(1)+mstatus(0) from CPU_SPECIAL slots (the nesting mret may
 * modify mepc), then POP_CALLER_REG which skips those slots. */
.macro POP_IRQ_REG
    LREG  t0, 1 * REGBYTES(sp)
    csrw  mepc, t0
    LREG  t0, 0 * REGBYTES(sp)
    csrw  mstatus, t0
    POP_CALLER_REG
.endm

/* g_mcause: tracks the previous mcause for nested interrupt bookkeeping
 * (preemption path only). */
#ifdef LOSCFG_ARCH_INTERRUPT_PREEMPTION
.section .bss
.align 4
.global g_mcause
g_mcause:
    .dword 0
#endif

/* CSR numbers for LinxCore131 himideerv200 PLIC nesting.
 * prithd (0xBFE): priority threshold — only interrupts with priority
 *   higher than this value can preempt.
 * cipri  (0x7ED): current interrupt priority — read to get the active
 *   interrupt's priority, then write to prithd to set the nesting floor.
 * (Also defined in riscv_himideerv200_plic.h for C code.) */
.equ PRITHD_CSR, 0xBFE
.equ CIPRI_CSR,  0x7ED

.section .interrupt.HalTrapVector.text, "ax"
.extern HalTrapEntry
.extern HalHwiInterruptDone
.extern OsSchedProcSchedFlag
.global HalTrapVector
.align 6
HalTrapVector:
    PUSH_CALLER_REG

    csrr  a0, mcause
    li    a1, TRAP_INTERRUPT_MODE_MASK
    li    a2, MCAUSE_INT_ID_MASK
    and   a1, a0, a1               /* a1 = interrupt bit               */
    and   a0, a0, a2               /* a0 = interrupt number (hwiNum)   */

    beqz  a1, HalTrapEntry         /* not interrupt → exception        */
    li    a2, NUM_HAL_INTERRUPT_NMI
    beq   a0, a2, HalTrapEntry     /* NMI → exception handler          */

    /* Save mstatus + mepc at slots 0/1 (within PUSH_CALLER_REG frame)
     * for POP_IRQ_REG to restore after the nesting mechanism may have
     * modified mepc via mret. */
    csrr  t0, mstatus
    SREG  t0, 0 * REGBYTES(sp)
    csrr  t0, mepc
    SREG  t0, 1 * REGBYTES(sp)

#ifdef LOSCFG_ARCH_INTERRUPT_PREEMPTION
    /* ---- Hardware interrupt nesting (LinxCore131-specific) ----
     * Read prithd to determine if this is the first interrupt (prithd==0)
     * or a nested one (prithd!=0). Only the first interrupt swaps stacks. */
    csrr  a1, PRITHD_CSR
    beqz  a1, 1f                   /* first interrupt → swap stack      */
    j     2f                       /* nested → keep current stack      */
1:
    csrw  mscratch, sp
    la    sp, __start_and_irq_stack_top
2:
    /* Allocate 4*REGBYTES (sp must be 16-aligned when -mpush-pop is on).
     * Slot 0 = old prithd, slot 1 = old g_mcause. */
    addi  sp, sp, -(4 * REGBYTES)
    SREG  a1, 0 * REGBYTES(sp)     /* save old prithd                   */
    csrr  a1, CIPRI_CSR           /* read current interrupt priority   */
    csrw  PRITHD_CSR, a1          /* set threshold = current prio      */

    /* Save/restore g_mcause for nesting bookkeeping */
    la    a1, g_mcause
    lw    t0, 0(a1)
    SREG  t0, 1 * REGBYTES(sp)     /* save old g_mcause                 */
    csrr  t1, mcause
    sw    t1, 0(a1)                /* update g_mcause                   */

    /* Timer interrupt: do not enable nesting (stay interrupt-disabled) */
    li    a1, NUM_HAL_INTERRUPT_TIMER
    beq   a0, a1, 3f

    /* Clear MPIE so mret leaves MIE=0 (nesting is via PRITHD, not MIE).
     * mret is a jump-to-3f trick (mepc←3f); mepc saved at slot 1 above. */
    li    t0, RISCV_MSTATUS_MPIE
    csrrc zero, mstatus, t0
    la    a1, 3f
    csrw  mepc, a1
    mret                           /* MPIE←1, MIE←0, PC←mepc(=3f)       */
3:
    jal   HalHwiInterruptDone      /* a0 = hwiNum (preserved)           */

    /* Restore g_mcause */
    la    a0, g_mcause
    LREG  t0, 1 * REGBYTES(sp)
    csrw  mcause, t0
    sw    t0, 0(a0)

    /* Restore prithd */
    LREG  a0, 0 * REGBYTES(sp)
    csrw  PRITHD_CSR, a0
    addi  sp, sp, 4 * REGBYTES
    beqz  a0, 4f                   /* first interrupt → restore mscratch */

    POP_IRQ_REG
    mret                           /* return from nested interrupt      */
#else
    /* ---- Non-preemption path (simple, like default trap) ---- */
    csrw  mscratch, sp
    la    sp, __start_and_irq_stack_top
    jal   HalHwiInterruptDone      /* a0 = hwiNum                       */
    csrr  sp, mscratch
#endif

4:
    csrr  sp, mscratch
    call  OsSchedProcSchedFlag

    POP_IRQ_REG
    mret
#endif /* _LOS_TRAP_S */
