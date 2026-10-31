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
#ifdef LOSCFG_BOX
/* BOX dual-stack offsets, generated from the live LosTaskCB by the
 * target's box/offsets action; staleness-checked by los_offsets.c.
 * Resolved via the include path the board config provides
 * (targets/<board>/liteos_m/box/kernel) - no hardcoded target path. */
#include "los_offsets.h"
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
#ifdef LOSCFG_BOX
    ldrh    r2, [r0, #TASK_CB_STATUS]
    tst     r2, #OS_TASK_STATUS_INUSER
    bne     __StartUserTask
#endif
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

#ifdef LOSCFG_BOX
__StartUserTask:
    /* BOX: first dispatch of a user task. The MPU view was already
     * switched to this task's box by OsSchedStart() (C, right before
     * ArchStartToRun), so this path is pure register loading — no
     * C calls, no stack usage, no PSP dependency. */
    ldr     r12, [r0, #TASK_CB_USER_SP]
    adds    r12, r12, #36              /* skip r4-r11 + primask */
    ldmfd   r12!, {r0-r7}              /* r0-r3, r12, lr, pc, xpsr */
    msr     psp, r12
    ldr     r2, =g_sysMode
    movs    r3, #1
    str     r3, [r2]
    movs    r2, #3                    /* CONTROL: PSP + unprivileged */
    mov     lr, r5
    cpsid   i                         /* airtight: masked until the
                                       * trampoline's msr primask - no
                                       * kernel-flash window at all */
    movs    r12, #0                   /* PRIMASK: fresh task runs unmasked */
    mov     r3, r6                    /* entry pc (thumb bit from the C ptr) */
    b       OsBoxUsrEntry             /* protocol r12/r2/r3 - see the
                                       * trampoline. Note: r2/r3 do not carry
                                       * the task's initial args (box entry
                                       * tasks take r0 = taskId only; r0/r1
                                       * ride through untouched) */
#endif
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
#ifdef LOSCFG_BOX
    /* BOX: a task switched out in user mode keeps its context on
     * the user stack; record it, stackPointer stays at kernel sp. */
    ldrh    r2, [r6, #TASK_CB_STATUS]
    tst     r2, #OS_TASK_STATUS_INUSER
    beq     1f
    str     r0, [r6, #TASK_CB_USER_SP]
    ldr     r0, [r6]
1:
#endif
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
#ifdef LOSCFG_BOX
    /* BOX: switch MPU to the new box (only on box change). */
    ldr     r2, [r0, #TASK_CB_BOX_CB]
    ldr     r3, [r6, #TASK_CB_BOX_CB]
    cmp     r2, r3
    beq     2f
    push    {r0, lr}
    mov     r0, r2
    bl      BoxMpuSwitch
    pop     {r0, lr}
2:
    /* Mode + dual-stack select: pick the user stack and drop
     * privilege for a task suspended in user mode. */
    ldrh    r2, [r0, #TASK_CB_STATUS]
    mrs     r3, control
    bic     r3, r3, #1
    tst     r2, #OS_TASK_STATUS_INUSER
    itt     ne
    ldrne   r1, [r0, #TASK_CB_USER_SP]
    orrne   r3, r3, #1
    msr     control, r3
    ldr     r2, =g_sysMode
    and     r3, r3, #1
    str     r3, [r2]
#endif
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


#ifdef LOSCFG_BOX
/* BOX user entry/return trampoline. Tail-jumped from __StartUserTask AND
 * the syscall epilogue (los_svc_entry.S) while still privileged and
 * MASKED, with
 *   r12 = PRIMASK to restore, r2 = CONTROL value (PSP | nPRIV),
 *   r3  = user pc (thumb bit set).
 * Must live in .usr.text (COMMON area, MPU R0): the unmask (msr primask)
 * has to run privileged, so doing it in kernel flash opens a window
 * where an interrupt (with reschedule) saves a kernel-flash pc with
 * INUSER set - the resume then refetches it unprivileged and the MPU
 * denies kernel flash (hardware-confirmed crash class, design doc
 * pitfall 25). Here the first unmasked instant sits inside COMMON_TEXT:
 * a window reschedule saves a COMMON_TEXT pc, resumes legally, and the
 * refetched msr are no-ops in unprivileged mode (PendSV already
 * restored the equivalent state). Protocol registers are all
 * caller-saved: r4-r11 carry live user values on the syscall-return
 * path and must ride through untouched (the veneer's pop {r0-r3}
 * restores r0-r3 after the return); r12 has held the epilogue's saved
 * PRIMASK since its first instruction. */
    .section .usr.text, "ax"
    .global OsBoxUsrEntry
    .type   OsBoxUsrEntry, %function
    .thumb_func
OsBoxUsrEntry:
    msr     primask, r12
    msr     control, r2
    isb
    bx      r3
#endif
