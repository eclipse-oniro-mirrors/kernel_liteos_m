/*
 * Copyright (c) 2013-2020, Huawei Technologies Co., Ltd. All rights reserved.
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

#include "arch/stack_ops.h"

.global  HalEnableIRQ
.global  HalDisableIRQ
.global  ArchIntLock
.global  ArchIntUnLock
.global  ArchIntRestore
.global  ArchStartToRun
.global  ArchTaskSchedule
.section .interrupt.text, "ax"

/*
 * a0: new task
 * a1: run task
 */
.align   4
ArchTaskSchedule:
    li      t0, LOS_MSTATUS_MIE | LOS_MSTATUS_MPIE
    csrrc   t1, MSTATUS, t0          /* clear MIE/MPIE, t1 = old mstatus */
    addi    sp, sp, -INT_SIZE_ON_STACK
    andi    t1, t1, LOS_MSTATUS_MIE  /* preserve old MIE */
    ori     t1, t1, 0x180            /* MPP = Machine */
    slli    t1, t1, 0x4              /* shift into MPP field */
    csrs    MSTATUS, t1
    csrw    MEPC, ra                 /* resume here after mret */

SaveRunTask:
    PUSH_CALLEE_REG
#ifndef LOSCFG_ARCH_FPU_DISABLE
    /* Save fcsr into the FPU_STATUS slot of the pre-allocated frame.
     * t2 is safe here: caller-saved regs are dead at this C call site. */
    frcsr   t2
    SREG    t2, FCSR_STACK_OFFSET(sp)
#endif
    csrr    t0, MSTATUS
    SREG    t0, 0 * REGBYTES(sp)     /* slot 0 = mstatus */
    csrr    t0, MEPC
    SREG    t0, 1 * REGBYTES(sp)     /* slot 1 = mepc */
#ifdef LOSCFG_LCMP_CUSTOM_INST16_EXTENSIONS
    /* Clear UOP cause bits in mcause (LinxCore170 LCMP extension). */
    li      t0, LOS_MCAUSE_UOP
    csrrc   zero, MCAUSE, t0
#endif

    sw      sp, TASK_CB_KERNEL_SP(a1)   /* save runTask sp */

/* a0: new task */
SwitchNewTask:
    /* retrieve stack pointer */
    lw      sp, TASK_CB_KERNEL_SP(a0)

    /* retrieve the registers (mstatus/mepc from slot 0/1, then full frame) */
    POP_ALL_REG

    mret

/*
 * a0: new task
 */
.align   4
.section .text, "ax"
ArchStartToRun:
    /* disable interrupts */
    li      t0, LOS_MSTATUS_MIE
    csrrc   zero, MSTATUS, t0

    /* a0 is new task, save it on g_newTask (OsCurrTaskGet relies on this) */
    la      t1, g_newTask
    sw      a0, 0(t1)

    tail    SwitchNewTask

HalDisableIRQ:
    li      t0, (LOS_MSTATUS_MPIE | LOS_MSTATUS_MIE) /* mpie | mie */
    csrrc   zero, MSTATUS, t0
    ret

HalEnableIRQ:
    csrsi  mstatus, LOS_MSTATUS_MIE
    ret

ArchIntLock:
    csrr    a0, mstatus           /* return value */
    li      t0, LOS_MSTATUS_MIE   /* mie */
    csrrc   zero, mstatus, t0
    ret

ArchIntUnLock:
    csrr    a0, mstatus           /* return value */
    li      t0, LOS_MSTATUS_MIE   /* mie */
    csrrs   zero, mstatus, t0
    ret

ArchIntRestore:
    csrw mstatus, a0
    ret
