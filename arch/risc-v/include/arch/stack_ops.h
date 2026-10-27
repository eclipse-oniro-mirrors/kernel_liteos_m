/* ----------------------------------------------------------------------------
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Description: RISC-V stack-frame save/restore macros. Single
 *              INT_SIZE_ON_STACK frame split into layered regions
 *              (CPU_SPECIAL/FPU/INTEGER_CALLEE/INTEGER_CALLER); TES/INT_AUTO/
 *              300S/VECTOR/SHARED stripped, FPU via !LOSCFG_ARCH_FPU_DISABLE.
 *              OHOS symbol names and exception-context structure preserved.
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
 * --------------------------------------------------------------------------- */

#ifndef _ARCH_STACK_OPS_H
#define _ARCH_STACK_OPS_H

#include "arch/cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

.extern TrapHandle
.extern OsSchedProcSchedFlag

/* INT_SIZE_ON_STACK plus the 4-word EXC_SPECIAL region (CXCPTSC/mcause/mtval/gp)
 * prepended only when handling an exception. */
#define EXC_SIZE_ON_STACK       (INT_SIZE_ON_STACK + EXC_SPECIAL_REG_SIZE)

/* FPU caller-saved: ft0-ft11, fa0-fa7 (20 regs) at the FPU_CALLER region. */
.macro  PUSH_FPU_CALLER_REG
    FSREG  ft11,  0 * FPUREGBYTES(sp)
    FSREG  ft10,  1 * FPUREGBYTES(sp)
    FSREG  ft9,   2 * FPUREGBYTES(sp)
    FSREG  ft8,   3 * FPUREGBYTES(sp)
    FSREG  fa7,   4 * FPUREGBYTES(sp)
    FSREG  fa6,   5 * FPUREGBYTES(sp)
    FSREG  fa5,   6 * FPUREGBYTES(sp)
    FSREG  fa4,   7 * FPUREGBYTES(sp)
    FSREG  fa3,   8 * FPUREGBYTES(sp)
    FSREG  fa2,   9 * FPUREGBYTES(sp)
    FSREG  fa1,  10 * FPUREGBYTES(sp)
    FSREG  fa0,  11 * FPUREGBYTES(sp)
    FSREG  ft7,  12 * FPUREGBYTES(sp)
    FSREG  ft6,  13 * FPUREGBYTES(sp)
    FSREG  ft5,  14 * FPUREGBYTES(sp)
    FSREG  ft4,  15 * FPUREGBYTES(sp)
    FSREG  ft3,  16 * FPUREGBYTES(sp)
    FSREG  ft2,  17 * FPUREGBYTES(sp)
    FSREG  ft1,  18 * FPUREGBYTES(sp)
    FSREG  ft0,  19 * FPUREGBYTES(sp)
.endm

.macro  POP_FPU_CALLER_REG
    FLREG  ft11,  0 * FPUREGBYTES(sp)
    FLREG  ft10,  1 * FPUREGBYTES(sp)
    FLREG  ft9,   2 * FPUREGBYTES(sp)
    FLREG  ft8,   3 * FPUREGBYTES(sp)
    FLREG  fa7,   4 * FPUREGBYTES(sp)
    FLREG  fa6,   5 * FPUREGBYTES(sp)
    FLREG  fa5,   6 * FPUREGBYTES(sp)
    FLREG  fa4,   7 * FPUREGBYTES(sp)
    FLREG  fa3,   8 * FPUREGBYTES(sp)
    FLREG  fa2,   9 * FPUREGBYTES(sp)
    FLREG  fa1,  10 * FPUREGBYTES(sp)
    FLREG  fa0,  11 * FPUREGBYTES(sp)
    FLREG  ft7,  12 * FPUREGBYTES(sp)
    FLREG  ft6,  13 * FPUREGBYTES(sp)
    FLREG  ft5,  14 * FPUREGBYTES(sp)
    FLREG  ft4,  15 * FPUREGBYTES(sp)
    FLREG  ft3,  16 * FPUREGBYTES(sp)
    FLREG  ft2,  17 * FPUREGBYTES(sp)
    FLREG  ft1,  18 * FPUREGBYTES(sp)
    FLREG  ft0,  19 * FPUREGBYTES(sp)
.endm

/* FPU callee-saved: fs0-fs11 (12 regs) at sp+\base. */
.macro  PUSH_FPU_CALLEE_REG base
    FSREG  fs11, (\base +  0 * FPUREGBYTES)(sp)
    FSREG  fs10, (\base +  1 * FPUREGBYTES)(sp)
    FSREG  fs9,  (\base +  2 * FPUREGBYTES)(sp)
    FSREG  fs8,  (\base +  3 * FPUREGBYTES)(sp)
    FSREG  fs7,  (\base +  4 * FPUREGBYTES)(sp)
    FSREG  fs6,  (\base +  5 * FPUREGBYTES)(sp)
    FSREG  fs5,  (\base +  6 * FPUREGBYTES)(sp)
    FSREG  fs4,  (\base +  7 * FPUREGBYTES)(sp)
    FSREG  fs3,  (\base +  8 * FPUREGBYTES)(sp)
    FSREG  fs2,  (\base +  9 * FPUREGBYTES)(sp)
    FSREG  fs1,  (\base + 10 * FPUREGBYTES)(sp)
    FSREG  fs0,  (\base + 11 * FPUREGBYTES)(sp)
.endm

/* Restore all 32 FP regs via base in t0 (callee fs0-fs11 then caller). */
.macro  POP_FPU_GENERAL_REG
    FLREG  fs11,  0 * FPUREGBYTES(t0)
    FLREG  fs10,  1 * FPUREGBYTES(t0)
    FLREG  fs9,   2 * FPUREGBYTES(t0)
    FLREG  fs8,   3 * FPUREGBYTES(t0)
    FLREG  fs7,   4 * FPUREGBYTES(t0)
    FLREG  fs6,   5 * FPUREGBYTES(t0)
    FLREG  fs5,   6 * FPUREGBYTES(t0)
    FLREG  fs4,   7 * FPUREGBYTES(t0)
    FLREG  fs3,   8 * FPUREGBYTES(t0)
    FLREG  fs2,   9 * FPUREGBYTES(t0)
    FLREG  fs1,  10 * FPUREGBYTES(t0)
    FLREG  fs0,  11 * FPUREGBYTES(t0)
    FLREG  ft11, 12 * FPUREGBYTES(t0)
    FLREG  ft10, 13 * FPUREGBYTES(t0)
    FLREG  ft9,  14 * FPUREGBYTES(t0)
    FLREG  ft8,  15 * FPUREGBYTES(t0)
    FLREG  fa7,  16 * FPUREGBYTES(t0)
    FLREG  fa6,  17 * FPUREGBYTES(t0)
    FLREG  fa5,  18 * FPUREGBYTES(t0)
    FLREG  fa4,  19 * FPUREGBYTES(t0)
    FLREG  fa3,  20 * FPUREGBYTES(t0)
    FLREG  fa2,  21 * FPUREGBYTES(t0)
    FLREG  fa1,  22 * FPUREGBYTES(t0)
    FLREG  fa0,  23 * FPUREGBYTES(t0)
    FLREG  ft7,  24 * FPUREGBYTES(t0)
    FLREG  ft6,  25 * FPUREGBYTES(t0)
    FLREG  ft5,  26 * FPUREGBYTES(t0)
    FLREG  ft4,  27 * FPUREGBYTES(t0)
    FLREG  ft3,  28 * FPUREGBYTES(t0)
    FLREG  ft2,  29 * FPUREGBYTES(t0)
    FLREG  ft1,  30 * FPUREGBYTES(t0)
    FLREG  ft0,  31 * FPUREGBYTES(t0)
.endm

/* INTEGER caller-saved: t6,t5,t4,t3,a7..a0,t2,t1,t0,ra (16 regs, low→high). */
.macro  PUSH_INTEGER_CALLER_REG base
    addi   sp, sp, -INTEGER_CALLER_REG_SIZE
    SREG   t6,  (\base +  0 * REGBYTES)(sp)
    SREG   t5,  (\base +  1 * REGBYTES)(sp)
    SREG   t4,  (\base +  2 * REGBYTES)(sp)
    SREG   t3,  (\base +  3 * REGBYTES)(sp)
    SREG   a7,  (\base +  4 * REGBYTES)(sp)
    SREG   a6,  (\base +  5 * REGBYTES)(sp)
    SREG   a5,  (\base +  6 * REGBYTES)(sp)
    SREG   a4,  (\base +  7 * REGBYTES)(sp)
    SREG   a3,  (\base +  8 * REGBYTES)(sp)
    SREG   a2,  (\base +  9 * REGBYTES)(sp)
    SREG   a1,  (\base + 10 * REGBYTES)(sp)
    SREG   a0,  (\base + 11 * REGBYTES)(sp)
    SREG   t2,  (\base + 12 * REGBYTES)(sp)
    SREG   t1,  (\base + 13 * REGBYTES)(sp)
    SREG   t0,  (\base + 14 * REGBYTES)(sp)
    SREG   ra,  (\base + 15 * REGBYTES)(sp)
.endm

.macro  POP_INTEGER_CALLER_REG base
    LREG   t6,  (\base +  0 * REGBYTES)(sp)
    LREG   t5,  (\base +  1 * REGBYTES)(sp)
    LREG   t4,  (\base +  2 * REGBYTES)(sp)
    LREG   t3,  (\base +  3 * REGBYTES)(sp)
    LREG   a7,  (\base +  4 * REGBYTES)(sp)
    LREG   a6,  (\base +  5 * REGBYTES)(sp)
    LREG   a5,  (\base +  6 * REGBYTES)(sp)
    LREG   a4,  (\base +  7 * REGBYTES)(sp)
    LREG   a3,  (\base +  8 * REGBYTES)(sp)
    LREG   a2,  (\base +  9 * REGBYTES)(sp)
    LREG   a1,  (\base + 10 * REGBYTES)(sp)
    LREG   a0,  (\base + 11 * REGBYTES)(sp)
    LREG   t2,  (\base + 12 * REGBYTES)(sp)
    LREG   t1,  (\base + 13 * REGBYTES)(sp)
    LREG   t0,  (\base + 14 * REGBYTES)(sp)
    LREG   ra,  (\base + 15 * REGBYTES)(sp)
    addi   sp, sp, INTEGER_CALLER_REG_SIZE
.endm

/* INTEGER callee-saved: s11..s0 (12 regs, low→high). */
.macro  PUSH_INTEGER_CALLEE_REG base
    SREG   s11, (\base +  0 * REGBYTES)(sp)
    SREG   s10, (\base +  1 * REGBYTES)(sp)
    SREG   s9,  (\base +  2 * REGBYTES)(sp)
    SREG   s8,  (\base +  3 * REGBYTES)(sp)
    SREG   s7,  (\base +  4 * REGBYTES)(sp)
    SREG   s6,  (\base +  5 * REGBYTES)(sp)
    SREG   s5,  (\base +  6 * REGBYTES)(sp)
    SREG   s4,  (\base +  7 * REGBYTES)(sp)
    SREG   s3,  (\base +  8 * REGBYTES)(sp)
    SREG   s2,  (\base +  9 * REGBYTES)(sp)
    SREG   s1,  (\base + 10 * REGBYTES)(sp)
    SREG   s0,  (\base + 11 * REGBYTES)(sp)
.endm

.macro  POP_INTEGER_CALLEE_REG base
    LREG   s11, (\base +  0 * REGBYTES)(sp)
    LREG   s10, (\base +  1 * REGBYTES)(sp)
    LREG   s9,  (\base +  2 * REGBYTES)(sp)
    LREG   s8,  (\base +  3 * REGBYTES)(sp)
    LREG   s7,  (\base +  4 * REGBYTES)(sp)
    LREG   s6,  (\base +  5 * REGBYTES)(sp)
    LREG   s5,  (\base +  6 * REGBYTES)(sp)
    LREG   s4,  (\base +  7 * REGBYTES)(sp)
    LREG   s3,  (\base +  8 * REGBYTES)(sp)
    LREG   s2,  (\base +  9 * REGBYTES)(sp)
    LREG   s1,  (\base + 10 * REGBYTES)(sp)
    LREG   s0,  (\base + 11 * REGBYTES)(sp)
.endm

/* Allocate lower regions (INTEGER_CALLEE, FPU, CPU_SPECIAL) after
 * PUSH_INTEGER_CALLER_REG. sp ends at CPU_SPECIAL; saves fcsr to the
 * FPU_STATUS slot. t2 is scratch (caller regs already stored). */
.macro  PUSH_OTHER_CALLER_REG
    addi  sp, sp, -INTEGER_CALLEE_REG_SIZE
#ifndef LOSCFG_ARCH_FPU_DISABLE
    addi  sp, sp, -(FPU_STATUS_REG_SIZE + FPU_CALLER_REG_SIZE)
    PUSH_FPU_CALLER_REG
    addi  sp, sp, -FPU_CALLEE_REG_SIZE
#endif
    addi  sp, sp, -CPU_SPECIAL_REG_SIZE
#ifndef LOSCFG_ARCH_FPU_DISABLE
    frcsr  t2
    SREG   t2,  FCSR_STACK_OFFSET(sp)
#endif
.endm

/* Full caller-side frame (interrupt entry). CPU_SPECIAL slots 0/1 left for
 * the path to fill mstatus/mepc (dispatch) or mcause/mepc (trap). */
.macro  PUSH_CALLER_REG
    PUSH_INTEGER_CALLER_REG 0
    PUSH_OTHER_CALLER_REG
.endm

/* Release the full caller frame; CPU_SPECIAL is skipped (mstatus/mepc were
 * restored by the caller via csrw). */
.macro  POP_CALLER_REG
    addi  sp, sp, CPU_SPECIAL_REG_SIZE
#ifndef LOSCFG_ARCH_FPU_DISABLE
    addi  sp, sp, FPU_CALLEE_REG_SIZE
    POP_FPU_CALLER_REG
    addi  sp, sp, (FPU_CALLER_REG_SIZE + FPU_STATUS_REG_SIZE)
#endif
    addi  sp, sp, INTEGER_CALLEE_REG_SIZE
    POP_INTEGER_CALLER_REG 0
.endm

/* Save only callee-saved (s0-s11 + FPU callee) for ArchTaskSchedule —
 * caller-saved are dead at a C call site by the ABI. The caller has already
 * pre-decremented sp by INT_SIZE_ON_STACK; this only writes the callee slots. */
.macro  PUSH_CALLEE_REG
#ifndef LOSCFG_ARCH_FPU_DISABLE
    PUSH_FPU_CALLEE_REG CPU_SPECIAL_REG_SIZE
    PUSH_INTEGER_CALLEE_REG (INTEGER_CALLEE_POSITION)
#else
    PUSH_INTEGER_CALLEE_REG CPU_SPECIAL_REG_SIZE
#endif
.endm

/* Restore the full frame to resume a task. mstatus/mepc from CPU_SPECIAL
 * slots 0/1, then FPU + INTEGER_CALLEE, then release and restore caller. */
.macro  POP_ALL_REG
    lw      t0, 1 * REGBYTES(sp)
    csrw    MEPC, t0
    lw      t0, 0 * REGBYTES(sp)
    csrw    MSTATUS, t0
#ifndef LOSCFG_ARCH_FPU_DISABLE
    lw     t0, FCSR_STACK_OFFSET(sp)
    fscsr  t0
    addi   t0, sp, CPU_SPECIAL_REG_SIZE
    POP_FPU_GENERAL_REG
    POP_INTEGER_CALLEE_REG (INTEGER_CALLEE_POSITION)
#else
    POP_INTEGER_CALLEE_REG CPU_SPECIAL_REG_SIZE
#endif
    addi  sp, sp, INT_SIZE_ON_STACK - INTEGER_CALLER_REG_SIZE
    POP_INTEGER_CALLER_REG 0
.endm

/* Like POP_ALL_REG but mstatus/mepc already restored by the caller (the C
 * exception entry updates them via g_excInfo). */
.macro  POP_ALL_REG_IN_EXCPTION
#ifndef LOSCFG_ARCH_FPU_DISABLE
    lw     t0, FCSR_STACK_OFFSET(sp)
    fscsr  t0
    addi   t0, sp, CPU_SPECIAL_REG_SIZE
    POP_FPU_GENERAL_REG
    POP_INTEGER_CALLEE_REG (INTEGER_CALLEE_POSITION)
#else
    POP_INTEGER_CALLEE_REG CPU_SPECIAL_REG_SIZE
#endif
    addi  sp, sp, INT_SIZE_ON_STACK - INTEGER_CALLER_REG_SIZE
    POP_INTEGER_CALLER_REG 0
.endm

/* Restore from an exception frame: EXC_SPECIAL (4 words) prepended below
 * INT_SIZE_ON_STACK, so mstatus/mepc are at slots 4/5. */
.macro  RESTORE_ALL
    LREG  t0, 4 * REGBYTES(sp)
    csrw  MSTATUS, t0
    LREG  t0, 5 * REGBYTES(sp)
    csrw  MEPC, t0

#ifndef LOSCFG_ARCH_FPU_DISABLE
    lw     t0, (EXC_SPECIAL_REG_SIZE + FCSR_STACK_OFFSET)(sp)
    fscsr  t0
    addi   t0, sp, (EXC_SPECIAL_REG_SIZE + CPU_SPECIAL_REG_SIZE)
    POP_FPU_GENERAL_REG
    POP_INTEGER_CALLEE_REG (EXC_SPECIAL_REG_SIZE + INTEGER_CALLEE_POSITION)
#else
    POP_INTEGER_CALLEE_REG (EXC_SPECIAL_REG_SIZE + CPU_SPECIAL_REG_SIZE)
#endif
    addi  sp, sp, (EXC_SIZE_ON_STACK - INTEGER_CALLER_REG_SIZE)
    POP_INTEGER_CALLER_REG 0
.endm

/* Swap sp with mscratchcswl (TES conditional form stripped; unconditional). */
.macro  INTERRUPT_STACK_SWAP tmp0 tmp1
    csrrw   sp, MSCRATCHCSWL, sp
.endm

#ifdef __cplusplus
}
#endif

#endif /* _ARCH_STACK_OPS_H */
