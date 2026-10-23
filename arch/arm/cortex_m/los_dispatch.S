/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
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
 *
 * Cortex-M task dispatch and context switch.
 *
 * M0+ (ARMv6-M, Thumb-1): no FPU, no IT blocks, movs only, push/pop R0-R7.
 * M4 (ARMv7-M, Thumb-2): optional FPU, Secure support.
 */

.syntax unified
#ifdef LOSCFG_ARCH_ARM_V6M
.arch armv6-m
#else
.arch armv7e-m
.fpu fpv4-sp-d16
#endif
.thumb

.equ    OS_FPU_CPACR,                0xE000ED88
.equ    OS_FPU_CPACR_ENABLE,         0x00F00000
.equ    OS_NVIC_INT_CTRL,            0xE000ED04
.equ    OS_NVIC_SYSPRI2,             0xE000ED20
.equ    OS_NVIC_PENDSV_PRI,          0xF0F00000
.equ    OS_NVIC_PENDSVSET,           0x10000000
.equ    OS_TASK_STATUS_RUNNING,      0x0010
.equ    OS_CONTROL_FPCA_ENABLE,      0x4
.equ    OS_CONTROL_KERNEL_MODE,      0x2
#if (LOSCFG_SECURE == 1)
.equ    OS_CONTROL_USER_MODE,        0x3
.equ    OS_TASK_FLAG_USER_TASK,      0x0200
#endif

    .section .text
    .thumb

#ifndef LOSCFG_ARCH_ARM_V6M
.macro SIGNAL_CONTEXT_RESTORE
    push    {r12, lr}
    blx     OsSignalTaskContextRestore
    pop     {r12, lr}
    cmp     r0, #0
    beq     1f
    mov     r1, r0
#if (LOSCFG_SECURE == 1)
    /* r0 = task owning the restored signal context (same task the C
     * helper checked: g_oldTask under LOSCFG_SCHED_LATENCY, else g_runTask) */
#ifdef LOSCFG_SCHED_LATENCY
    ldr     r0, =g_oldTask
#else
    ldr     r0, =g_runTask
#endif
    ldr     r0, [r0]
#endif
    b       SignalContextRestore
1:
.endm
#endif

    .type ArchStartToRun, %function
    .global ArchStartToRun
ArchStartToRun:
    .fnstart
    .cantunwind

    ldr     r4, =OS_NVIC_SYSPRI2
    ldr     r5, =OS_NVIC_PENDSV_PRI
    str     r5, [r4]

    ldr     r1, =g_oldTask
    str     r0, [r1]

    /* Switch to privileged Thread mode using PSP */
    movs    r2, #OS_CONTROL_KERNEL_MODE
    msr     control, r2
#ifdef LOSCFG_ARCH_ARM_V6M
    isb

    /* r1 = newTask->stackPointer (offset 0) */
    ldr     r1, [r0]

    /* Set PSP past the entire TaskContext (36 bytes).
     * Future stack pushes grow downward from here. */
    adds    r1, #36
    msr     psp, r1

    /* Load R0 (taskID), LR (ArchTaskExit), PC (OsTaskEntry) from context.
     *
     * We CANNOT use exception return (bx 0xFFFFFFFD) from Thread mode:
     * on ARMv6-M this is UNPREDICTABLE — the core treats it as a regular
     * branch to 0xFFFFFFFC, causing a HardFault.
     * Instead, we branch directly to OsTaskEntry, matching the M7 port.
     */
    ldr     r1, [r0]          /* r1 = context_base */
    ldr     r0, [r1, #36]     /* r0 = uwR0 = taskID (first arg) */
    ldr     r5, [r1, #56]     /* r5 = uwLR = ArchTaskExit */
    ldr     r6, [r1, #60]     /* r6 = uwPC = OsTaskEntry */
    mov     lr, r5
    cpsie   i
    bx      r6
#else
    ldr     r12, [r0]

    ldr.w   r1, =OS_FPU_CPACR
    ldr     r1, [r1]
    and     r1, r1, #OS_FPU_CPACR_ENABLE
    cmp     r1, #OS_FPU_CPACR_ENABLE
    bne     __DisabledFPU
    add     r12, r12, #100

    ldmfd   r12!, {r0-r7}
    add     r12, r12, #72
    msr     psp, r12
    vpush   {s0}
    vpop    {s0}
    mov     lr, r5
    cpsie   i
    bx      r6

__DisabledFPU:
    add     r12, r12, #36

    ldmfd   r12!, {r0-r7}
    msr     psp, r12
    mov     lr, r5
    cpsie   i
    bx      r6
#endif

    .fnend

    .type ArchIntLock, %function
    .global ArchIntLock
ArchIntLock:
    .fnstart
    .cantunwind

    mrs     r0, primask
    cpsid   i
    bx      lr
    .fnend

    .type ArchIntUnLock, %function
    .global ArchIntUnLock
ArchIntUnLock:
    .fnstart
    .cantunwind

    mrs     r0, primask
    cpsie   i
    bx      lr
    .fnend

    .type ArchIntRestore, %function
    .global ArchIntRestore
ArchIntRestore:
    .fnstart
    .cantunwind

    msr     primask, r0
    bx      lr
    .fnend

    .type ArchTaskSchedule, %function
    .global ArchTaskSchedule
ArchTaskSchedule:
    .fnstart
    .cantunwind

    ldr     r0, =OS_NVIC_INT_CTRL
    ldr     r1, =OS_NVIC_PENDSVSET
    str     r1, [r0]
    mrs     r1, ipsr
    cmp     r1, #0
    bne     ArchTaskScheduleExit
    mrs     r0, primask
    cpsie   i
    msr     primask, r0
ArchTaskScheduleExit:
    bx      lr
    .fnend

    .type HalPendSV, %function
    .global HalPendSV
HalPendSV:
    .fnstart
    .cantunwind

#ifdef LOSCFG_ARCH_ARM_V6M
    mrs     r1, primask
    cpsid   i
#else
    mrs     r12, primask
    cpsid   i

HalTaskSwitch:
    SIGNAL_CONTEXT_RESTORE
#endif

    /* Check if actual switch needed: g_runTask != g_oldTask */
    ldr     r0, =g_runTask
    ldr     r2, [r0]
    ldr     r3, =g_oldTask
    ldr     r3, [r3]
    cmp     r2, r3
    beq     HalPendSVNoSwitch

#ifdef LOSCFG_ARCH_ARM_V6M
    /* Save context: R4-R11 + PRIMASK (36 bytes below PSP) */
    mrs     r0, psp
    subs    r0, #36
    stmia   r0!, {r4-r7}
    mov     r4, r8
    mov     r5, r9
    mov     r6, r10
    mov     r7, r11
    stmia   r0!, {r4-r7}
    str     r1, [r0]             /* PRIMASK at context_base + 32 */
    subs    r0, #32              /* r0 = context_base */

    /* g_oldTask->stackPointer = context_base */
    str     r0, [r3]             /* r3 = g_oldTask (task ptr) */

    /* g_oldTask = g_runTask */
    ldr     r0, =g_oldTask
    str     r2, [r0]

    /* r0 = newTask->stackPointer */
    ldr     r0, [r2]
#else
    /* Save context: R4-R12 (+ optional FPU) */
    mrs     r0, psp
    stmfd   r0!, {r4-r12}
    ldr.w   r3, =OS_FPU_CPACR
    ldr     r3, [r3]
    and     r3, r3, #OS_FPU_CPACR_ENABLE
    cmp     r3, #OS_FPU_CPACR_ENABLE
    bne     __DisabledFPU1
    vstmdb  r0!, {d8-d15}

__DisabledFPU1:
    /* g_oldTask->stackPointer = saved SP */
    ldr     r5, =g_oldTask
    ldr     r6, [r5]
    str     r0, [r6]

    /* g_oldTask = g_runTask */
    ldr     r0, =g_runTask
    ldr     r0, [r0]
    str     r0, [r5]

    /* r1 = newTask->stackPointer */
    ldr     r1, [r0]
#endif

#ifdef LOSCFG_ARCH_ARM_V6M
    /* Restore R8-R11 (offset 16-28) */
    adds    r0, #16
    ldmia   r0!, {r4-r7}
    mov     r8, r4
    mov     r9, r5
    mov     r10, r6
    mov     r11, r7

    /* Restore R4-R7 (offset 0-12) */
    subs    r0, #32
    ldmia   r0!, {r4-r7}

    /* Load PRIMASK (offset 32), set PSP, restore PRIMASK */
    adds    r0, #16
    ldr     r1, [r0]
    adds    r0, #4
    msr     psp, r0
    msr     primask, r1
#else
SignalContextRestore:
    ldr.w   r3, =OS_FPU_CPACR
    ldr     r3, [r3]
    and     r3, r3, #OS_FPU_CPACR_ENABLE
    cmp     r3, #OS_FPU_CPACR_ENABLE
    bne     __DisabledFPU2
    vldmia  r1!, {d8-d15}

__DisabledFPU2:
#if (LOSCFG_SECURE == 1)
    ldrh    r7, [r0, #4]
    tst     r7, #OS_TASK_FLAG_USER_TASK
    ite     eq
    moveq   r3, #OS_CONTROL_KERNEL_MODE
    movne   r3, #OS_CONTROL_USER_MODE
    msr     control, r3
#endif
    ldmfd   r1!, {r4-r12}
    msr     psp,  r1
    msr     primask, r12
#endif

    bx      lr

HalPendSVNoSwitch:
#ifdef LOSCFG_ARCH_ARM_V6M
    msr     primask, r1
#else
    msr     primask, r12
#endif
    bx      lr

    .fnend
