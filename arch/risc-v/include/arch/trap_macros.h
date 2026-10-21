/*
 * Copyright (c) 2013-2020, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Description: Shared trap macros for RISC-V interrupt entry.
 *              Included by los_trap.S (default) and linx131/trap.S (131).
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
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
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

#ifndef _TRAP_MACROS_H
#define _TRAP_MACROS_H

#include "soc_common.h"

/* Save caller-saved registers (t0-t6, a0-a7, ra) + mstatus + mepc + FPU state.
 * Stack frame: 32 integer slots + FPU area. Slots 0-1 reserved for
 * mstatus/mepc (used by nesting path in linx131/trap.S). */
.macro PUSH_CALLER_REG
    addi sp, sp, -(32 * REGBYTES + FPU_SAVE_SIZE)
    SREG  t6, 2 * REGBYTES(sp)
    SREG  t5, 3 * REGBYTES(sp)
    SREG  t4, 4 * REGBYTES(sp)
    SREG  t3, 5 * REGBYTES(sp)
    SREG  t2, 6 * REGBYTES(sp)
    SREG  t1, 7 * REGBYTES(sp)
    SREG  t0, 8 * REGBYTES(sp)
    SREG  a7, 18 * REGBYTES(sp)
    SREG  a6, 19 * REGBYTES(sp)
    SREG  a5, 20 * REGBYTES(sp)
    SREG  a4, 21 * REGBYTES(sp)
    SREG  a3, 22 * REGBYTES(sp)
    SREG  a2, 23 * REGBYTES(sp)
    SREG  a1, 24 * REGBYTES(sp)
    SREG  a0, 25 * REGBYTES(sp)
    SREG  ra, 31 * REGBYTES(sp)
    csrr  t0, mstatus
    SREG  t0, 16 * REGBYTES(sp)
    csrr  t0, mepc
    SREG  t0, 17 * REGBYTES(sp)
#ifndef LOSCFG_ARCH_FPU_DISABLE
    FSREG  ft11, (32 * REGBYTES + 0  * FPUREGBYTES)(sp)
    FSREG  ft10, (32 * REGBYTES + 1  * FPUREGBYTES)(sp)
    FSREG  ft9,  (32 * REGBYTES + 2  * FPUREGBYTES)(sp)
    FSREG  ft8,  (32 * REGBYTES + 3  * FPUREGBYTES)(sp)
    FSREG  fa7,  (32 * REGBYTES + 4  * FPUREGBYTES)(sp)
    FSREG  fa6,  (32 * REGBYTES + 5  * FPUREGBYTES)(sp)
    FSREG  fa5,  (32 * REGBYTES + 6  * FPUREGBYTES)(sp)
    FSREG  fa4,  (32 * REGBYTES + 7  * FPUREGBYTES)(sp)
    FSREG  fa3,  (32 * REGBYTES + 8  * FPUREGBYTES)(sp)
    FSREG  fa2,  (32 * REGBYTES + 9  * FPUREGBYTES)(sp)
    FSREG  fa1,  (32 * REGBYTES + 10 * FPUREGBYTES)(sp)
    FSREG  fa0,  (32 * REGBYTES + 11 * FPUREGBYTES)(sp)
    FSREG  ft7,  (32 * REGBYTES + 12 * FPUREGBYTES)(sp)
    FSREG  ft6,  (32 * REGBYTES + 13 * FPUREGBYTES)(sp)
    FSREG  ft5,  (32 * REGBYTES + 14 * FPUREGBYTES)(sp)
    FSREG  ft4,  (32 * REGBYTES + 15 * FPUREGBYTES)(sp)
    FSREG  ft3,  (32 * REGBYTES + 16 * FPUREGBYTES)(sp)
    FSREG  ft2,  (32 * REGBYTES + 17 * FPUREGBYTES)(sp)
    FSREG  ft1,  (32 * REGBYTES + 18 * FPUREGBYTES)(sp)
    FSREG  ft0,  (32 * REGBYTES + 19 * FPUREGBYTES)(sp)
    frcsr  t0
    SREG   t0, FCSR_STACK_OFFSET(sp)
#endif
.endm

/* Restore caller-saved registers + mstatus + mepc + FPU state. */
.macro POP_CALLER_REG
    LREG  t0, 16 * REGBYTES(sp)
    csrw  mstatus, t0
#ifndef LOSCFG_ARCH_FPU_DISABLE
    LREG   t0, FCSR_STACK_OFFSET(sp)
    fscsr  t0
    FLREG  ft11, (32 * REGBYTES + 0  * FPUREGBYTES)(sp)
    FLREG  ft10, (32 * REGBYTES + 1  * FPUREGBYTES)(sp)
    FLREG  ft9,  (32 * REGBYTES + 2  * FPUREGBYTES)(sp)
    FLREG  ft8,  (32 * REGBYTES + 3  * FPUREGBYTES)(sp)
    FLREG  fa7,  (32 * REGBYTES + 4  * FPUREGBYTES)(sp)
    FLREG  fa6,  (32 * REGBYTES + 5  * FPUREGBYTES)(sp)
    FLREG  fa5,  (32 * REGBYTES + 6  * FPUREGBYTES)(sp)
    FLREG  fa4,  (32 * REGBYTES + 7  * FPUREGBYTES)(sp)
    FLREG  fa3,  (32 * REGBYTES + 8  * FPUREGBYTES)(sp)
    FLREG  fa2,  (32 * REGBYTES + 9  * FPUREGBYTES)(sp)
    FLREG  fa1,  (32 * REGBYTES + 10 * FPUREGBYTES)(sp)
    FLREG  fa0,  (32 * REGBYTES + 11 * FPUREGBYTES)(sp)
    FLREG  ft7,  (32 * REGBYTES + 12 * FPUREGBYTES)(sp)
    FLREG  ft6,  (32 * REGBYTES + 13 * FPUREGBYTES)(sp)
    FLREG  ft5,  (32 * REGBYTES + 14 * FPUREGBYTES)(sp)
    FLREG  ft4,  (32 * REGBYTES + 15 * FPUREGBYTES)(sp)
    FLREG  ft3,  (32 * REGBYTES + 16 * FPUREGBYTES)(sp)
    FLREG  ft2,  (32 * REGBYTES + 17 * FPUREGBYTES)(sp)
    FLREG  ft1,  (32 * REGBYTES + 18 * FPUREGBYTES)(sp)
    FLREG  ft0,  (32 * REGBYTES + 19 * FPUREGBYTES)(sp)
#endif
    LREG  t0, 17 * REGBYTES(sp)
    csrw  mepc, t0
    LREG  t6, 2 * REGBYTES(sp)
    LREG  t5, 3 * REGBYTES(sp)
    LREG  t4, 4 * REGBYTES(sp)
    LREG  t3, 5 * REGBYTES(sp)
    LREG  t2, 6 * REGBYTES(sp)
    LREG  t1, 7 * REGBYTES(sp)
    LREG  t0, 8 * REGBYTES(sp)
    LREG  a7, 18 * REGBYTES(sp)
    LREG  a6, 19 * REGBYTES(sp)
    LREG  a5, 20 * REGBYTES(sp)
    LREG  a4, 21 * REGBYTES(sp)
    LREG  a3, 22 * REGBYTES(sp)
    LREG  a2, 23 * REGBYTES(sp)
    LREG  a1, 24 * REGBYTES(sp)
    LREG  a0, 25 * REGBYTES(sp)
    LREG  ra, 31 * REGBYTES(sp)
    addi sp, sp, 32 * REGBYTES + FPU_SAVE_SIZE
.endm

#endif /* _TRAP_MACROS_H */
