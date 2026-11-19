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
 * Cortex-M task dispatch and context switch — BOX (user-mode isolation)
 * variant, compiled instead of los_dispatch.S when LOSCFG_BOX is set.
 * Delta vs the vanilla dispatcher: dual-stack PendSV hooks
 * (BOX_PENDSV_SAVE / BOX_PENDSV_RESTORE from los_box_dispatch.h) and the
 * unified FP context geometry (in-body notes).
 *
 * ARMv7-M only (this target: M4, optional FPU). The ARMv6-M branches the
 * vanilla dispatcher carries are gone - BOX_PENDSV_RESTORE already needed
 * Thumb-2 (IT blocks), so a V6M BOX build never assembled in the first
 * place; the #error below fails it early with a message instead. The
 * KERNEL_SIGNAL restore path is gone too: it branched straight past
 * BOX_PENDSV_RESTORE, which is an isolation break under BOX (see the #error
 * in los_box_dispatch.h); SIGNAL support would need that path rewritten to
 * re-establish box state first. LOSCFG_SECURE (TrustZone) never applied to
 * this file's target and its only consumer was the removed signal path.
 */

.syntax unified
.arch armv7e-m
.fpu fpv4-sp-d16
.thumb

.equ    OS_FPU_CPACR,                0xE000ED88
.equ    OS_FPU_CPACR_ENABLE,         0x00F00000
.equ    OS_NVIC_INT_CTRL,            0xE000ED04
.equ    OS_NVIC_SYSPRI2,             0xE000ED20
.equ    OS_NVIC_PENDSV_PRI,          0xF0F00000
.equ    OS_NVIC_PENDSVSET,           0x10000000
/* OS_TASK_STATUS_RUNNING / OS_CONTROL_FPCA_ENABLE from the vanilla file were
 * dropped: their only ever consumers were the removed signal/secure paths
 * (this file never set RUNNING itself - the C layer owns task status). */
.equ    OS_CONTROL_KERNEL_MODE,      0x2
/* BOX dual-stack PendSV hooks + the asm-side OS_TASK_STATUS_INUSER home
 * (BOX-only consumers: this file, los_svc_entry.S, los_box_uspace.S). */
#include "los_box_dispatch.h"

/* ARMv6-M is not supported by the BOX dispatch: BOX_PENDSV_SAVE/RESTORE and
 * the SVC entry are Thumb-2 (IT blocks, ldrh/tst combinations), so a V6M BOX
 * build never assembled. The V6M branches of the vanilla dispatcher were
 * removed from this file; fail the combination explicitly rather than with a
 * confusing assembler error deep inside the hooks. */
#ifdef LOSCFG_ARCH_ARM_V6M
#error "LOSCFG_ARCH_ARM_V6M is not supported together with LOSCFG_BOX: the BOX dispatch (los_dispatch_box.S) is ARMv7-M only (BOX_PENDSV_SAVE/RESTORE need Thumb-2)."
#endif

    .section .text
    .thumb

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

    /* BOX note (no branch needed): the first task ever dispatched here is
     * always a KERNEL task — box init runs from HelloworldTask after the
     * scheduler started, so user tasks only ever enter through the PendSV
     * INUSER path. */
    ldr     r12, [r0]

    ldr.w   r1, =OS_FPU_CPACR
    ldr     r1, [r1]
    and     r1, r1, #OS_FPU_CPACR_ENABLE
    cmp     r1, #OS_FPU_CPACR_ENABLE
    bne     __DisabledFPU
    /* Initial context: [d8-d15 FP_GAP][r4-r12+priMask TASK_CTX_R0][HW frame
     * 32B][FP area FP_TAIL]. Skip the d8-d15 slot and the saved r4-r12 block
     * to reach the hardware frame. Both operands come from the generated
     * los_offsets.h (via los_box_dispatch.h) - they used to be literal 100
     * and 72 here, which silently misplaced the frame if EXC_CONTEXT_S or
     * TaskContext ever changed shape. */
    add     r12, r12, #(EXC_CTX_FP_GAP + TASK_CTX_R0)

    ldmfd   r12!, {r0-r7}
    /* r12 is now at the end of the HW frame; skip the FP area so PSP starts
     * at the stack top exactly like a PendSV save does. */
    add     r12, r12, #EXC_CTX_FP_TAIL
    msr     psp, r12
    vpush   {s0}
    vpop    {s0}
    mov     lr, r5
    cpsie   i
    bx      r6

__DisabledFPU:
    /* No d8-d15 slot in this layout, so only the r4-r12+priMask block is
     * skipped (TASK_CTX_R0, previously the literal 36). */
    add     r12, r12, #TASK_CTX_R0

    ldmfd   r12!, {r0-r7}
    msr     psp, r12
    mov     lr, r5
    cpsie   i
    bx      r6

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

    mrs     r12, primask
    cpsid   i

    /* Check if actual switch needed: g_runTask != g_oldTask */
    ldr     r0, =g_runTask
    ldr     r2, [r0]
    ldr     r3, =g_oldTask
    ldr     r3, [r3]
    cmp     r2, r3
    beq     HalPendSVNoSwitch

    /* Save context: R4-R12 (+ optional FPU) */
    mrs     r0, psp
    ldr.w   r3, =OS_FPU_CPACR
    ldr     r3, [r3]
    and     r3, r3, #OS_FPU_CPACR_ENABLE
    cmp     r3, #OS_FPU_CPACR_ENABLE
    beq     6f

__DisabledFPU1:
    subs    r0, r0, #36
    stmia   r0, {r4-r12}
    b       7f
6:
    /* Task switch is a FULL FP save: the pending lazy save (FPCCR.LSPACT)
     * still holds S0-S15/FPSCR in the registers. Any FP access forces the
     * hardware to commit them to the exception frame's FP area, which is
     * exactly what the restore path reads back. Without this the outgoing
     * task's S0-S15 survive only until the next task touches the FPU.
     *
     * Layout, identical to ArchTaskStackInit/OsBoxUsrStackInit:
     *   [base,      base+36)  r4-r12
     *   [base+36,   base+100) d8-d15 (S16-S31)
     * base = psp-100 is 4-mod-8, so base+36 = psp-64 is 8-byte aligned:
     * VLDM/VSTM of d8-d15 must never run at a 4-mod-8 address (that is an
     * UNALIGNED UsageFault on this core), hence the explicit "+36" instead
     * of the natural vstmdb r0!, which would land on psp-36. */
    vmrs    r3, fpscr
    subs    r0, r0, #100
    stmia   r0, {r4-r12}
    adds    r2, r0, #36
    vstmia  r2, {d8-d15}
7:

    /* g_oldTask->stackPointer = saved SP */
    ldr     r5, =g_oldTask
    ldr     r6, [r5]
    BOX_PENDSV_SAVE
    str     r0, [r6]

    /* g_oldTask = g_runTask */
    ldr     r0, =g_runTask
    ldr     r0, [r0]
    str     r0, [r5]

    /* r1 = newTask->stackPointer */
    ldr     r1, [r0]

    BOX_PENDSV_RESTORE
    ldr.w   r3, =OS_FPU_CPACR
    ldr     r3, [r3]
    and     r3, r3, #OS_FPU_CPACR_ENABLE
    cmp     r3, #OS_FPU_CPACR_ENABLE
    bne     __DisabledFPU2
    /* Saved layout: r4-r12 at ctx[0..36), d8-d15 at ctx+36 (8-byte
     * aligned), PSP = ctx+100. A plain "vldmia r1!, {d8-d15}" would run at
     * ctx+36 only after the 36-byte r4-r12 pop; the explicit offset keeps
     * the FP access off a 4-mod-8 address. */
    ldmia   r1, {r4-r12}
    adds    r1, r1, #36
    vldmia  r1!, {d8-d15}
    b       8f

__DisabledFPU2:
    ldmfd   r1!, {r4-r12}
8:
    msr     psp,  r1
    msr     primask, r12

    bx      lr

HalPendSVNoSwitch:
    msr     primask, r12
    bx      lr

    .fnend
