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
 * Cortex-M exception handling (M4 + M0+ via LOSCFG_ARCH_ARM_V6M).
 *
 * M0+ (ARMv6-M, Thumb-1): HardFault-only — no CFSR/HFSR/BFAR/MMAR, no
 *   separate MemManage/Bus/Usage faults (all escalate to HardFault);
 *   context detection via EXC_RETURN bit 2 (NVIC active array on M0+
 *   does not cover system exceptions like PendSV/SysTick/SVC).
 * M4 (ARMv7-M, Thumb-2): full fault state register analysis.
 */

.syntax unified
#ifdef LOSCFG_ARCH_ARM_V6M
.arch armv6-m
#else
.arch armv7e-m
.fpu fpv4-sp-d16
#endif
.thumb
.section .text


    .global  HalExcNMI
    .global  HalExcHardFault
    .global  HalExcMemFault
    .global  HalExcBusFault
    .global  HalExcUsageFault
    .global  HalExcSvcCall

    .extern HalExcHandleEntry
#ifndef LOSCFG_ARCH_ARM_V6M
    .extern g_uwExcTbl
#endif
    .extern g_taskScheduled

.equ OS_FLG_BGD_ACTIVE,   0x0001

.equ OS_EXC_CAUSE_NMI,   16
.equ OS_EXC_CAUSE_HARDFAULT,   17

.equ HF_DEBUGEVT,   20
.equ HF_VECTBL,   21

.equ FLAG_ADDR_VALID,   0x10000
.equ FLAG_HWI_ACTIVE,   0x20000
.equ FLAG_NO_FLOAT,     0x10000000

.equ OS_NVIC_FSR,           0xE000ED28      //include BusFault/MemFault/UsageFault State Register
.equ OS_NVIC_HFSR,          0xE000ED2C      //HardFault State Register
.equ OS_NVIC_BFAR,          0xE000ED38
.equ OS_NVIC_MMAR,          0xE000ED34
.equ OS_NVIC_ACT_BASE,      0xE000E300
.equ OS_NVIC_SHCSRS,        0xE000ED24
.equ OS_NVIC_SHCSR_MASK,    0xC00

    .type HalExcNMI, %function
    .global HalExcNMI
HalExcNMI:
    .fnstart
    .cantunwind
    movs    r0, #OS_EXC_CAUSE_NMI
    movs    r1, #0
    b       osExcDispatch
    .fnend

    .type HalExcHardFault, %function
    .global HalExcHardFault
HalExcHardFault:
    .fnstart
    .cantunwind
#ifdef LOSCFG_ARCH_ARM_V6M
    movs    r0, #OS_EXC_CAUSE_HARDFAULT
    movs    r1, #0
    b       osExcDispatch
    .fnend
#else
    mov  r0, #OS_EXC_CAUSE_HARDFAULT
    ldr  r2, =OS_NVIC_HFSR
    ldr  r2, [r2]

    mov  r1, #HF_DEBUGEVT
    orr  r0, r0, r1, LSL #0x8
    tst  r2, #0x80000000
    bne  osExcDispatch     // DEBUGEVT

    and  r0, r0 , #0x000000FF
    mov  r1, #HF_VECTBL
    orr  r0, r0, r1, LSL #0x8
    tst  r2, #0x00000002
    bne  osExcDispatch     // VECTBL

    //if not DEBUGEVT and VECTBL then is FORCED
    and  r0, r0, #0x000000FF

    ldr  r2, =OS_NVIC_FSR
    ldr  r2, [r2]

    tst  r2, #0x8000     // BFARVALID
    bne  _HFBusFault     // BusFault

    tst  r2, #0x80       // MMARVALID
    bne  _HFMemFault     // MemFault

    mov  r12,#0
    b    osHFExcCommonBMU
    .fnend

    .type _HFBusFault, %function

_HFBusFault:
    .fnstart
    .cantunwind
    ldr  r1, =OS_NVIC_BFAR
    ldr  r1, [r1]
    mov  r12, #FLAG_ADDR_VALID
    b    osHFExcCommonBMU
    .fnend

    .type _HFMemFault, %function

_HFMemFault:
    .fnstart
    .cantunwind
    ldr  r1, =OS_NVIC_MMAR
    ldr  r1, [r1]
    mov  r12, #FLAG_ADDR_VALID
    .fnend

    .type osHFExcCommonBMU, %function
    .global osHFExcCommonBMU
osHFExcCommonBMU:
    .fnstart
    .cantunwind
    clz  r2, r2
    ldr  r3, =g_uwExcTbl
    add  r3, r3, r2
    ldrb r2, [r3]
    orr  r0, r0, r2, LSL #0x8
    orr  r0, r0 ,r12
    b    osExcDispatch
    .fnend

    .type HalExcSvcCall, %function
    .global HalExcSvcCall
HalExcSvcCall:
    .fnstart
    .cantunwind
    tst   lr, #0x4
    ite   eq
    mrseq r1, msp
    bne   _svcCallFromPsp
    b     _svcCall
_svcCallFromPsp:
#ifdef LOSCFG_SECURE
    push  {r0-r12, lr}
    mov   r0, sp
    cpsie I
    bl    OsSyscallHandle
    cpsid I
    mrs   r12, psp
    stm   r12, {r0-r1}
    pop   {r0-r12, lr}
    bx    lr
#endif
    mrs   r1, psp
_svcCall:
    ldr   r0, [r1,#24]
    ldrb  r0, [r0,#-2]
    mov   r1, #0
    b     osExcDispatch
    .fnend

    .type HalExcBusFault, %function
    .global HalExcBusFault
HalExcBusFault:
    .fnstart
    .cantunwind
    ldr  r0, =OS_NVIC_FSR
    ldr  r0, [r0]

    tst  r0, #0x8000  // BFARVALID
    beq  _ExcBusNoADDR
    ldr  r1, =OS_NVIC_BFAR
    ldr  r1, [r1]
    mov  r12, #FLAG_ADDR_VALID
    and  r0, r0, #0x1F00

    b    osExcCommonBMU
    .fnend

    .type _ExcBusNoADDR, %function
    .global _ExcBusNoADDR
_ExcBusNoADDR:
    .fnstart
    .cantunwind
    mov  r12,#0
    b    osExcCommonBMU
    .fnend

    .type HalExcMemFault, %function
    .global HalExcMemFault
HalExcMemFault:
    .fnstart
    .cantunwind
    ldr  r0, =OS_NVIC_FSR
    ldr  r0, [r0]

    tst  r0, #0x80 // MMARVALID
    beq  _ExcMemNoADDR
    ldr  r1, =OS_NVIC_MMAR
    ldr  r1, [r1]
    mov  r12, #FLAG_ADDR_VALID
    and  r0, r0, #0x1B

    b    osExcCommonBMU
    .fnend

    .type _ExcMemNoADDR, %function
    .global _ExcMemNoADDR
_ExcMemNoADDR:
    .fnstart
    .cantunwind
    mov  r12,#0
    b    osExcCommonBMU
    .fnend

    .type HalExcUsageFault, %function
    .global HalExcUsageFault
HalExcUsageFault:
    .fnstart
    .cantunwind
    ldr  r0, =OS_NVIC_FSR
    ldr  r0, [r0]

    movw r1, #0x030F
    lsl  r1, r1, #16
    and  r0, r0, r1
    mov  r12, #0

    .fnend

    .type osExcCommonBMU, %function
    .global osExcCommonBMU
osExcCommonBMU:
    .fnstart
    .cantunwind
    clz  r0, r0
    ldr  r3, =g_uwExcTbl
    add  r3, r3, r0
    ldrb r0, [r3]
    orr  r0, r0, r12
    .fnend
// r0 -- EXCCAUSE(bit 16 is 1 if EXCADDR valid),  r1 -- EXCADDR
#endif /* !LOSCFG_ARCH_ARM_V6M */

#ifdef LOSCFG_ARCH_ARM_V6M
    /*
     * ARMv6-M has no separate MemManage/Bus/Usage fault exceptions
     * (all faults escalate to HardFault) and no CFSR/HFSR/BFAR/MMAR
     * registers.  These stubs exist only because the shared NVIC driver
     * (drivers/interrupt/arm_nvic.c HalHwiInit) installs them into the
     * system vector slots unconditionally; on M0+ hardware the slots
     * are Reserved and never taken.
     */
    .type HalExcMemFault, %function
    .global HalExcMemFault
HalExcMemFault:
    .fnstart
    .cantunwind
    b       HalExcHardFault
    .fnend

    .type HalExcBusFault, %function
    .global HalExcBusFault
HalExcBusFault:
    .fnstart
    .cantunwind
    b       HalExcHardFault
    .fnend

    .type HalExcUsageFault, %function
    .global HalExcUsageFault
HalExcUsageFault:
    .fnstart
    .cantunwind
    b       HalExcHardFault
    .fnend

    .type HalExcSvcCall, %function
    .global HalExcSvcCall
HalExcSvcCall:
    .fnstart
    .cantunwind
    mrs     r1, psp
    ldr     r0, [r1, #24]
    subs    r0, r0, #2
    ldrb    r0, [r0]
    movs    r1, #0
    b       osExcDispatch
    .fnend
#endif /* LOSCFG_ARCH_ARM_V6M */

    .type osExcDispatch, %function
    .global osExcDispatch
osExcDispatch:
    .fnstart
    .cantunwind

#ifdef LOSCFG_ARCH_ARM_V6M
    /*
     * Use EXC_RETURN (LR) to determine the fault context.
     * Bit 2:  0 = msp  (handler mode or Thread+msp init)
     *         1 = psp  (Thread mode — task)
     *
     * r2 and r3 are safe to clobber: their task values are preserved
     * in the hardware frame and will be read from there.
     */
    mov     r2, lr
    movs    r3, #4
    tst     r2, r3
    bne     osExcFromTask

    /* ===========================================================
     * Fault from msp context (Handler mode or Thread+msp init).
     * The hardware frame is already on msp, contiguous after our
     * 40-byte save area — no copy needed.
     * =========================================================== */
    mov     r3, sp              /* r3 = msp after hardware push */

    /* Save r4-r11 + primask + sp on msp (10 words = 40 bytes) */
push_save_and_scan:
    sub     sp, sp, #40
    str     r4, [sp, #0]
    str     r5, [sp, #4]
    str     r6, [sp, #8]
    str     r7, [sp, #12]
    mov     r4, r8
    str     r4, [sp, #16]
    mov     r4, r9
    str     r4, [sp, #20]
    mov     r4, r10
    str     r4, [sp, #24]
    mov     r4, r11
    str     r4, [sp, #28]
    mrs     r4, primask
    str     r4, [sp, #32]
    adds    r3, #32             /* original sp before hardware push */
    str     r3, [sp, #36]

    /* r4-r7 now free — check NVIC active registers for external IRQ */
    ldr     r2, =OS_NVIC_ACT_BASE
    movs    r4, #0
osExcIrqLoop:
    ldr     r5, [r2, r4]
    cmp     r5, #0
    bne     osExcInIrq
    adds    r4, r4, #4
    cmp     r4, #32
    bne     osExcIrqLoop

    /* Not in external IRQ — check if scheduler started */
    ldr     r2, =g_taskScheduled
    ldr     r2, [r2]
    movs    r5, #OS_FLG_BGD_ACTIVE
    tst     r2, r5
    beq     osExcCallHandler    /* Init phase — no HWI flag */

    /* In a system exception handler (PendSV / SysTick / HardFault) */
    ldr     r5, =FLAG_HWI_ACTIVE
    orrs    r0, r0, r5
    movs    r1, #0
    b       osExcCallHandler

osExcInIrq:
    /* r5 = active bits, r4 = byte offset */
    ldr     r6, =FLAG_HWI_ACTIVE
    orrs    r0, r0, r6
    /* Find IRQ number by scanning set bits */
    movs    r2, #0
osExcIrqFindBit:
    lsrs    r5, r5, #1
    bcc     osExcIrqNextBit
    b       osExcIrqFound
osExcIrqNextBit:
    adds    r2, r2, #1
    cmp     r2, #32
    bne     osExcIrqFindBit
osExcIrqFound:
    lsrs    r4, r4, #2          /* byte offset / 4 = word index */
    lsls    r4, r4, #5          /* word index * 32 */
    adds    r2, r2, r4          /* full IRQ number */
    mov     r1, r2              /* pid = IRQ number */
    b       osExcCallHandler

    /* ===========================================================
     * Fault from psp context (Thread mode — task).
     * Hardware frame is on psp — must copy to msp for a
     * contiguous EXC_CONTEXT_S layout.
     * =========================================================== */
osExcFromTask:
    mrs     r3, psp             /* r3 = psp (hardware frame start) */

    /* Save r4-r11 + primask + sp on msp */
    sub     sp, sp, #40
    str     r4, [sp, #0]
    str     r5, [sp, #4]
    str     r6, [sp, #8]
    str     r7, [sp, #12]
    mov     r4, r8
    str     r4, [sp, #16]
    mov     r4, r9
    str     r4, [sp, #20]
    mov     r4, r10
    str     r4, [sp, #24]
    mov     r4, r11
    str     r4, [sp, #28]
    mrs     r4, primask
    str     r4, [sp, #32]
    /* Save task sp = psp + 32 (the original sp before hardware push) */
    mov     r4, r3
    adds    r4, #32
    str     r4, [sp, #36]

    /*
     * Copy the 8-word hardware frame from psp to sp+40 so that
     * EXC_CONTEXT_S is fully contiguous on msp.
     * r3 still points to psp.
     */
    ldmia   r3!, {r4-r7}        /* load r0, r1, r2, r3 from psp */
    mov     r2, sp
    adds    r2, #40             /* destination: sp + 40 */
    stmia   r2!, {r4-r7}        /* store r0-r3 */
    ldmia   r3!, {r4-r7}        /* load r12, lr, pc, xPSR from psp */
    stmia   r2!, {r4-r7}        /* store r12, lr, pc, xPSR */
    /* fall through */

osExcCallHandler:
    /* Set FLAG_NO_FLOAT (no FPU on Cortex-M0+) */
    ldr     r4, =FLAG_NO_FLOAT
    orrs    r0, r0, r4

    /* Call HalExcHandleEntry(excType, faultAddr, pid, excBufAddr) */
    mov     r3, sp              /* excBufAddr = saved context on msp */
    cpsid   i
    ldr     r4, =HalExcHandleEntry
    bx      r4
    .fnend
#else
    ldr   r2, =OS_NVIC_ACT_BASE
    mov   r12, #8                       // r12 is hwi check loop counter
    .fnend

    .type _hwiActiveCheck, %function
    .global _hwiActiveCheck
_hwiActiveCheck:
    .fnstart
    .cantunwind
    ldr   r3, [r2]                      // r3 store active hwi register when exc
    cmp   r3, #0
    beq   _hwiActiveCheckNext

    // exc occurred in IRQ
    orr   r0, r0, #FLAG_HWI_ACTIVE
    rbit  r2, r3
    clz   r2, r2
    and   r12, r12, #1
    add   r2, r2, r12, LSL #5               // calculate r2 (hwi number) as pid
    .fnend

    .type _ExcInMSP, %function
    .global _ExcInMSP
_ExcInMSP:
    .fnstart
    .cantunwind
    cmp   lr, #0xFFFFFFE9
    bne   _NoFloatInMsp
    add   r3, r13, #104
    push  {r3}
    mrs   r12, primask                  // store message-->exc: disable int?
    push {r4-r12}                       // store message-->exc: {r4-r12}
    vpush {d8-d15}                      // FPU
    b     _handleEntry
    .fnend

    .type _NoFloatInMsp, %function
    .global _NoFloatInMsp
_NoFloatInMsp:
    .fnstart
    .cantunwind
    add   r3, r13, #32
    push  {r3}                           // store message-->exc: msp(r13)

    mrs   r12, primask                   // store message-->exc: disable int?
    push {r4-r12}                        // store message-->exc: {r4-r12}
    orr   r0, r0, #FLAG_NO_FLOAT
    b     _handleEntry
    .fnend

    .type _hwiActiveCheckNext, %function
    .global _hwiActiveCheckNext
_hwiActiveCheckNext:
    .fnstart
    .cantunwind
    add   r2, r2, #4                        // next NVIC ACT ADDR
    subs  r12, r12, #1
    bne   _hwiActiveCheck

    /*NMI interrupt excption*/
    ldr   r2, =OS_NVIC_SHCSRS
    ldrh  r2,[r2]
    ldr   r3,=OS_NVIC_SHCSR_MASK
    and   r2, r2,r3
    cmp   r2,#0
    bne   _ExcInMSP
    // exc occurred in Task or Init or exc
    // reserved for register info from task stack

    ldr  r2, =g_taskScheduled
    ldr  r2, [r2]
    tst  r2, #OS_FLG_BGD_ACTIVE
    beq  _ExcInMSP                      // if exc occurred in Init then branch


    cmp   lr, #0xFFFFFFED               //auto push floating registers
    bne   _NoFloatInPsp

    // exc occurred in Task
    mov   r2,  sp
    sub   sp, #96                      // add 8 Bytes reg(for STMFD)

    mrs   r3,  psp
    add   r12, r3, #104
    push  {r12}                         // save task sp

    mrs   r12, primask
    push {r4-r12}
    vpush {d8-d15}                      // FPU

    // copy auto saved task register

    ldmfd r3!, {r4-r11}                 // r4-r11 store psp reg(auto push when exc in task)
    vldmia  r3!, {d8-d15}               // FPU
    vstmdb  r2!, {d8-d15}               // FPU
    stmfd r2!, {r4-r11}
    b     _handleEntry
    .fnend

    .type _NoFloatInPsp, %function
    .global _NoFloatInPsp
_NoFloatInPsp:
    .fnstart
    .cantunwind
    mov   r2,  sp                      // no auto push floating registers
    sub   sp, #32                      // add 8 Bytes reg(for STMFD)

    mrs   r3,  psp
    add   r12, r3, #32
    push  {r12}                         // save task sp

    mrs   r12, primask
    push {r4-r12}

    ldmfd r3, {r4-r11}                  // r4-r11 store psp reg(auto push when exc in task)
    stmfd r2!, {r4-r11}
    orr   r0, r0, #FLAG_NO_FLOAT
    .fnend

    .type _handleEntry, %function
    .global _handleEntry
_handleEntry:
    .fnstart
    .cantunwind
    mov r3, sp                         // sp:the 4th param
    cpsid I
    cpsid F
    b  HalExcHandleEntry

    nop
    .fnend
#endif /* LOSCFG_ARCH_ARM_V6M */
