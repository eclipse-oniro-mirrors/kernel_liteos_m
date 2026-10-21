/*
 * Copyright (c) 2013-2020, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Description: RISC-V default trap (interrupt) entry — HalTrapVector.
 *              LinxCore131 overrides it via src/linx131/trap.S
 *              (file replacement in BUILD.gn).
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

#ifndef _LOS_TRAP_S
#define _LOS_TRAP_S
#include "soc_common.h"
#include "trap_macros.h"

.section .interrupt.HalTrapVector.text, "ax"
.extern HalTrapEntry
.extern OsSchedProcSchedFlag
.global HalTrapVector
.equ TRAP_INTERRUPT_MODE_MASK, 0x80000000
.align 6
HalTrapVector:
    PUSH_CALLER_REG
    csrr  a0, mcause
    li    a1, TRAP_INTERRUPT_MODE_MASK
    li    a2, MCAUSE_INT_ID_MASK
    and   a1, a0, a1
    and   a0, a2, a0
    beqz  a1, HalTrapEntry
    csrw  mscratch, sp
    la    sp, __start_and_irq_stack_top
    jal   HalHwiInterruptDone
    csrr  sp, mscratch
    call  OsSchedProcSchedFlag

    POP_CALLER_REG
    mret
#endif /* _LOS_TRAP_S */
