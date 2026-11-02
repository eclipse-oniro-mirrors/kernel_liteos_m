/*
 * Copyright (c) 2013-2020, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Description: RISC-V exception entry — HalTrapEntry. Uses stack_ops.h
 *              macros but keeps OHOS symbols and the LosExcContext/LosExcInfo
 *              layout.
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

#ifndef _LOS_EXC_S
#define _LOS_EXC_S
#include "arch/stack_ops.h"

.section .interrupt.text, "ax"
.extern HalExcEntry
.extern g_excInfo
.global HalTrapEntry
.align 4

/* Exception (non-interrupt) entry. PUSH_CALLEE_REG + a 4-word EXC_SPECIAL
 * region forms a LosExcContext {mcause,mtval,medeleg,gp,taskContext}; a0
 * carries the masked cause set by the trap-dispatch path. HalExcEntry(a0=sp)
 * runs in C; on return mstatus/mepc are restored from CPU_SPECIAL slots 0/1. */
HalTrapEntry:
    PUSH_CALLEE_REG
    /* Save mstatus/mepc to CPU_SPECIAL slots 0/1 — POP_ALL_REG_IN_EXCPTION
     * does not restore them, and HalExcEntry only reads the context, so
     * they must be saved here for the restore path below. */
    csrr  t0, mstatus
    SREG  t0, 0 * REGBYTES(sp)        /* slot 0 = mstatus */
    csrr  t0, mepc
    SREG  t0, 1 * REGBYTES(sp)        /* slot 1 = mepc */
    addi  sp, sp, -EXC_SPECIAL_REG_SIZE
    sw    a0, 0 * REGBYTES(sp)        /* LosExcContext.mcause  */
    csrr  t0, mtval
    sw    t0, 1 * REGBYTES(sp)        /* LosExcContext.mtval   */
    sw    zero, 2 * REGBYTES(sp)      /* LosExcContext.medeleg */
    sw    gp, 3 * REGBYTES(sp)        /* LosExcContext.gp      */
    mv    a0, sp                      /* a0 = excBufAddr       */
    csrw  mscratch, sp
    la    t0, g_excInfo
    lh    t1, 0(t0)                   /* nestCnt */
    bnez  t1, 1f
    la    sp, __exc_stack_top
1:
    addi  t1, t1, 0x1
    sh    t1, 0(t0)                   /* nestCnt++ */
    call  HalExcEntry
    la    t0, g_excInfo
    sh    zero, 0(t0)                 /* nestCnt = 0 */
    csrr  sp, mscratch
    addi  sp, sp, EXC_SPECIAL_REG_SIZE  /* drop EXC_SPECIAL; sp→CPU_SPECIAL */
    lw    t0, 0 * REGBYTES(sp)        /* slot 0 = mstatus */
    csrw  mstatus, t0
    lw    t0, 1 * REGBYTES(sp)        /* slot 1 = mepc */
    csrw  mepc, t0
    POP_ALL_REG_IN_EXCPTION
    mret

#endif /* _LOS_EXC_S */
