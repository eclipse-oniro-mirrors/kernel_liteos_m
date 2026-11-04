/*
 * Copyright (c) 2013-2020, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Description: RISC-V arch-level reset entry (generic).
 *
 *              Provides the reset_vector / HandleReset path that:
 *                - installs the trap entry (HalTrapVector, los_trap.S) into mtvec
 *                - initializes gp (global pointer) and the startup stack
 *                - enables the FPU if LOSCFG_ARCH_FPU_DISABLE is not set
 *                - optionally seeds the stack canary
 *                - calls BoardConfig (board-level pin/clock setup) then main()
 *
 *              Excluded when LOSCFG_USING_BOARD_RESET_VECTOR is set, because
 *              in that case the board supplies its own reset_vector.S
 *              (e.g. ws63 board/reset_vector.S, which performs SRAM sharing
 *              config specific to the SoC and uses board-specific data/bss
 *              symbol names). This arch-level version is the generic fallback
 *              for boards that do not need custom reset logic.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 *    of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LOSCFG_USING_BOARD_RESET_VECTOR

#include "soc_common.h"

.extern HalTrapVector
.extern BoardConfig
.extern main

/* Symbols defined in los_stack.S (or by the board linker script when
 * LOSCFG_USING_BOARD_RESET_VECTOR is set — but we are excluded then). */
.global __startup_stack_top
.global __irq_stack_top
.global __exc_stack_top
.global __nmi_stack_top

.global reset_vector
.global HandleReset

.option rvc
.section .start.text, "ax"
.type   reset_vector, @function
reset_vector:
    .option push
    .option norelax
    tail HandleReset
    .option pop

.section .text, "ax"
.option rvc
HandleReset:
    .option push
    .option norelax
    /* Initialize global pointer. Use _gp_ (matches ws63 board.ld) to avoid
     * GCC gp-relaxation swapping it back to __global_pointer$. */
    la      gp, _gp_
    .option pop

    /* Point mtvec at the arch trap entry. mtvec is CPU-level trap
     * configuration — an arch concern, set here so the first exception/interrupt
     * is handled even before BoardConfig runs. */
    la      t0, HalTrapVector
    csrw    mtvec, t0

    /* Point mscratch at the IRQ stack top (used by the trap path to swap
     * sp on the first interrupt). */
    la      t0, __irq_stack_top
    csrw    mscratch, t0

    /* Clear mstatus / mie — start with interrupts off. */
    csrwi   mstatus, 0
    csrwi   mie, 0

#ifndef LOSCFG_ARCH_FPU_DISABLE
    /* Enable the FPU (mstatus.FS = Dirty) and clear fcsr so the first FP
     * instruction does not trap. Required because TaskContext contains FP
     * registers when FPU is on, so POP_ALL_REG executes flw/fscsr on the
     * first task switch. */
    li      t0, RISCV_MSTATUS_FS
    csrs    mstatus, t0
    fscsr   x0
#endif

    /* Initialize the startup stack pointer. */
    la      sp, __startup_stack_top

    /* Board-level setup (pin mux, clocks, etc.). */
    call    BoardConfig

    /* Enter the C entry point. */
    tail    main

    .size   HandleReset, . - HandleReset
#endif /* !LOSCFG_USING_BOARD_RESET_VECTOR */
