/*
 * Copyright (c) 2021 Nuclei Limited. All rights reserved.
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

#include "riscv_encoding.h"

#ifndef __riscv_32e
#define portRegNum          30
#else
#define portRegNum          14
#endif

#define portCONTEXT_SIZE    ( portRegNum * REGBYTES )

    .section .text
    .align 4

    .type ArchIntLock, %function
    .global ArchIntLock
ArchIntLock:
    csrr    a0, mstatus           // return value
    li      t0, MSTATUS_MIE   // mie
    csrrc   zero, mstatus, t0
    ret

    .type ArchIntUnLock, %function
    .global ArchIntUnLock
ArchIntUnLock:
    csrr    a0, mstatus           // return value
    li      t0, MSTATUS_MIE   // mie
    csrrs   zero, mstatus, t0
    ret

    .type ArchIntRestore, %function
    .global ArchIntRestore
ArchIntRestore:
    csrw mstatus, a0
    ret


/* Start the first task. a0 = newTask (LosTaskCB *), called by OsSchedStart()
   with interrupts still disabled; never returns to the caller. */
    .type ArchStartToRun, %function
    .global ArchStartToRun
    .align 3
ArchStartToRun:
    /* Setup Interrupt Stack using
       The stack that was used by main()
       before the scheduler is started is
       no longer required after the scheduler is started.
       Interrupt stack pointer is stored in CSR_MSCRATCH */
    la t0, _sp
    csrw CSR_MSCRATCH, t0
    csrw CSR_MSCRATCHCSWL, t0

    /* Publish the first task as both scheduler view (g_runTask) and
       context owner (g_oldTask); then load its stack pointer from the
       first TCB member. a0/t0 are safe here (dead at this call site). */
    la t1, g_runTask
    STORE a0, 0x0(t1)
    la t1, g_oldTask
    STORE a0, 0x0(t1)
    LOAD  sp, 0x0(a0)

    /* Pop PC from stack and set MEPC */
    LOAD t0,  0  * REGBYTES(sp)
    csrw CSR_MEPC, t0
    /* Pop mstatus from stack and set it */
    LOAD t0,  (portRegNum - 1)  * REGBYTES(sp)
    csrw CSR_MSTATUS, t0
    /* Interrupt still disable here */
    /* Restore Registers from Stack */
    LOAD x1,  1  * REGBYTES(sp)    /* RA */
    LOAD x5,  2  * REGBYTES(sp)
    LOAD x6,  3  * REGBYTES(sp)
    LOAD x7,  4  * REGBYTES(sp)
    LOAD x8,  5  * REGBYTES(sp)
    LOAD x9,  6  * REGBYTES(sp)
    LOAD x10, 7  * REGBYTES(sp)
    LOAD x11, 8  * REGBYTES(sp)
    LOAD x12, 9  * REGBYTES(sp)
    LOAD x13, 10 * REGBYTES(sp)
    LOAD x14, 11 * REGBYTES(sp)
    LOAD x15, 12 * REGBYTES(sp)
#ifndef __riscv_32e
    LOAD x16, 13 * REGBYTES(sp)
    LOAD x17, 14 * REGBYTES(sp)
    LOAD x18, 15 * REGBYTES(sp)
    LOAD x19, 16 * REGBYTES(sp)
    LOAD x20, 17 * REGBYTES(sp)
    LOAD x21, 18 * REGBYTES(sp)
    LOAD x22, 19 * REGBYTES(sp)
    LOAD x23, 20 * REGBYTES(sp)
    LOAD x24, 21 * REGBYTES(sp)
    LOAD x25, 22 * REGBYTES(sp)
    LOAD x26, 23 * REGBYTES(sp)
    LOAD x27, 24 * REGBYTES(sp)
    LOAD x28, 25 * REGBYTES(sp)
    LOAD x29, 26 * REGBYTES(sp)
    LOAD x30, 27 * REGBYTES(sp)
    LOAD x31, 28 * REGBYTES(sp)
#endif

    addi sp, sp, portCONTEXT_SIZE

    mret

.macro SAVE_FULL_FRAME
    addi sp, sp, -portCONTEXT_SIZE
    STORE x1,  1  * REGBYTES(sp)    /* RA */
    STORE x5,  2  * REGBYTES(sp)
    STORE x6,  3  * REGBYTES(sp)
    STORE x7,  4  * REGBYTES(sp)
    STORE x8,  5  * REGBYTES(sp)
    STORE x9,  6  * REGBYTES(sp)
    STORE x10, 7  * REGBYTES(sp)
    STORE x11, 8  * REGBYTES(sp)
    STORE x12, 9  * REGBYTES(sp)
    STORE x13, 10 * REGBYTES(sp)
    STORE x14, 11 * REGBYTES(sp)
    STORE x15, 12 * REGBYTES(sp)
#ifndef __riscv_32e
    STORE x16, 13 * REGBYTES(sp)
    STORE x17, 14 * REGBYTES(sp)
    STORE x18, 15 * REGBYTES(sp)
    STORE x19, 16 * REGBYTES(sp)
    STORE x20, 17 * REGBYTES(sp)
    STORE x21, 18 * REGBYTES(sp)
    STORE x22, 19 * REGBYTES(sp)
    STORE x23, 20 * REGBYTES(sp)
    STORE x24, 21 * REGBYTES(sp)
    STORE x25, 22 * REGBYTES(sp)
    STORE x26, 23 * REGBYTES(sp)
    STORE x27, 24 * REGBYTES(sp)
    STORE x28, 25 * REGBYTES(sp)
    STORE x29, 26 * REGBYTES(sp)
    STORE x30, 27 * REGBYTES(sp)
    STORE x31, 28 * REGBYTES(sp)
#endif
    csrr t0, CSR_MSTATUS
    STORE t0,  (portRegNum - 1) * REGBYTES(sp)
    csrr t0, CSR_MEPC
    STORE t0,  0 * REGBYTES(sp)
.endm

.macro LOAD_FULL_FRAME
    LOAD t0,  0 * REGBYTES(sp)
    csrw CSR_MEPC, t0
    LOAD t0,  (portRegNum - 1) * REGBYTES(sp)
    csrw CSR_MSTATUS, t0
    LOAD x1,  1  * REGBYTES(sp)    /* RA */
    LOAD x5,  2  * REGBYTES(sp)
    LOAD x6,  3  * REGBYTES(sp)
    LOAD x7,  4  * REGBYTES(sp)
    LOAD x8,  5  * REGBYTES(sp)
    LOAD x9,  6  * REGBYTES(sp)
    LOAD x10, 7  * REGBYTES(sp)
    LOAD x11, 8  * REGBYTES(sp)
    LOAD x12, 9  * REGBYTES(sp)
    LOAD x13, 10 * REGBYTES(sp)
    LOAD x14, 11 * REGBYTES(sp)
    LOAD x15, 12 * REGBYTES(sp)
#ifndef __riscv_32e
    LOAD x16, 13 * REGBYTES(sp)
    LOAD x17, 14 * REGBYTES(sp)
    LOAD x18, 15 * REGBYTES(sp)
    LOAD x19, 16 * REGBYTES(sp)
    LOAD x20, 17 * REGBYTES(sp)
    LOAD x21, 18 * REGBYTES(sp)
    LOAD x22, 19 * REGBYTES(sp)
    LOAD x23, 20 * REGBYTES(sp)
    LOAD x24, 21 * REGBYTES(sp)
    LOAD x25, 22 * REGBYTES(sp)
    LOAD x26, 23 * REGBYTES(sp)
    LOAD x27, 24 * REGBYTES(sp)
    LOAD x28, 25 * REGBYTES(sp)
    LOAD x29, 26 * REGBYTES(sp)
    LOAD x30, 27 * REGBYTES(sp)
    LOAD x31, 28 * REGBYTES(sp)
#endif
    addi sp, sp, portCONTEXT_SIZE
.endm

    .type ArchTaskSchedule, %function
    .global ArchTaskSchedule
    .align 2
ArchTaskSchedule:
    SAVE_FULL_FRAME

    csrr t0, CSR_MSTATUS
    li   t1, (MSTATUS_MIE | MSTATUS_MPIE | MSTATUS_MPP)
    not  t1, t1
    and  t0, t0, t1
    li   t1, MSTATUS_MPP
    or   t0, t0, t1
    STORE t0, (portRegNum - 1) * REGBYTES(sp)
    STORE ra, 0 * REGBYTES(sp)

    /* runTask->stackPointer = sp (a1 = runTask, still live) */
    STORE sp, 0(a1)
    /* g_oldTask = newTask: the CPU is about to load newTask's frame, the
       scheduler already published it as g_runTask via ArchCurrTaskSet. */
    la t0, g_oldTask
    STORE a0, 0(t0)

    /* newTask->stackPointer -> sp, resume its context */
    LOAD sp, 0(a0)
    LOAD_FULL_FRAME

    mret

    .align 2
    .global eclic_mtip_handler
eclic_mtip_handler:
    SAVE_FULL_FRAME

    /* OS_INT_ACTIVE bookkeeping for the ISR body */
    la t0, g_intCount
    lw t1, 0(t0)
    addi t1, t1, 1
    sw t1, 0(t0)

    jal ArchTickIsr

    la t0, g_intCount
    lw t1, 0(t0)
    addi t1, t1, -1
    sw t1, 0(t0)

    jal OsSchedProcSchedFlag

    li t0, 0
    csrw CSR_MINTSTATUS, t0

    LOAD_FULL_FRAME

    mret
