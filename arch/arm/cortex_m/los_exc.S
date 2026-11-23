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
 *
 * Cortex-M exception handling.
 * ARMv6-M: HardFault-only (no CFSR/HFSR/BFAR/MMAR, no separate
 *   MemManage/Bus/Usage faults); context detection via EXC_RETURN bit 2.
 * ARMv7-M: full fault state register analysis.
 *
 * Single source for GNU as and IAR iasmarm (see los_dispatch.S).
 * IAR: no ARMv6-M and no LOSCFG_SECURE (macros undefined for iasmarm).
 */

#include "los_arch_macro.h"

#ifdef __IASMARM__
    PRESERVE8
    SECTION .text:CODE(2)
    THUMB
    REQUIRE8
#else /* __GNUC__ */
.syntax unified
#ifdef LOSCFG_ARCH_ARM_V6M
.arch armv6-m
#else
.arch armv7e-m
.fpu fpv4-sp-d16
#endif
.thumb
.section .text
#endif

    EXTERNAL HalExcHandleEntry
#ifndef LOSCFG_ARCH_ARM_V6M
    EXTERNAL g_uwExcTbl
#endif
    EXTERNAL g_taskScheduled

    EQUAL OS_FLG_BGD_ACTIVE,   0x0001
    EQUAL OS_EXC_CAUSE_NMI,   16
    EQUAL OS_EXC_CAUSE_HARDFAULT,   17
    EQUAL HF_DEBUGEVT,   20
    EQUAL HF_VECTBL,   21
    EQUAL FLAG_ADDR_VALID,   0x10000
    EQUAL FLAG_HWI_ACTIVE,   0x20000
    EQUAL FLAG_NO_FLOAT,     0x10000000
    EQUAL OS_NVIC_FSR,           0xE000ED28      /* include BusFault/MemFault/UsageFault State Register */
    EQUAL OS_NVIC_HFSR,          0xE000ED2C      /* HardFault State Register */
    EQUAL OS_NVIC_BFAR,          0xE000ED38
    EQUAL OS_NVIC_MMAR,          0xE000ED34
    EQUAL OS_NVIC_ACT_BASE,      0xE000E300
    EQUAL OS_NVIC_SHCSRS,        0xE000ED24
    EQUAL OS_NVIC_SHCSR_MASK,    0xC00

    FUNC HalExcNMI
HalExcNMI:
    FNSTART
    CANTUNWIND
    movs    r0, #OS_EXC_CAUSE_NMI
    movs    r1, #0
    b       osExcDispatch
    FNEND

    FUNC HalExcHardFault
HalExcHardFault:
    FNSTART
    CANTUNWIND
#ifdef LOSCFG_ARCH_ARM_V6M
    movs    r0, #OS_EXC_CAUSE_HARDFAULT
    movs    r1, #0
    b       osExcDispatch
    FNEND
#else
    mov  r0, #OS_EXC_CAUSE_HARDFAULT
    ldr  r2, =OS_NVIC_HFSR
    ldr  r2, [r2]

    mov  r1, #HF_DEBUGEVT
    orr  r0, r0, r1, LSL #0x8
    tst  r2, #0x80000000
    bne  osExcDispatch     /* DEBUGEVT */

    and  r0, r0 , #0x000000FF
    mov  r1, #HF_VECTBL
    orr  r0, r0, r1, LSL #0x8
    tst  r2, #0x00000002
    bne  osExcDispatch     /* VECTBL */

    /* if not DEBUGEVT and VECTBL then is FORCED */
    and  r0, r0, #0x000000FF

    ldr  r2, =OS_NVIC_FSR
    ldr  r2, [r2]

    tst  r2, #0x8000     /* BFARVALID */
    bne  _HFBusFault     /* BusFault */

    tst  r2, #0x80       /* MMARVALID */
    bne  _HFMemFault     /* MemFault */

    mov  r12,#0
    b    osHFExcCommonBMU
    FNEND

    FUNCLOCAL _HFBusFault
_HFBusFault:
    FNSTART
    CANTUNWIND
    ldr  r1, =OS_NVIC_BFAR
    ldr  r1, [r1]
    mov  r12, #FLAG_ADDR_VALID
    b    osHFExcCommonBMU
    FNEND

    FUNCLOCAL _HFMemFault
_HFMemFault:
    FNSTART
    CANTUNWIND
    ldr  r1, =OS_NVIC_MMAR
    ldr  r1, [r1]
    mov  r12, #FLAG_ADDR_VALID
    FNEND

    FUNC osHFExcCommonBMU
osHFExcCommonBMU:
    FNSTART
    CANTUNWIND
    clz  r2, r2
    ldr  r3, =g_uwExcTbl
    add  r3, r3, r2
    ldrb r2, [r3]
    orr  r0, r0, r2, LSL #0x8
    orr  r0, r0 ,r12
    b    osExcDispatch
    FNEND

    FUNC HalExcSvcCall
HalExcSvcCall:
    FNSTART
    CANTUNWIND
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
    FNEND

    FUNC HalExcBusFault
HalExcBusFault:
    FNSTART
    CANTUNWIND
    ldr  r0, =OS_NVIC_FSR
    ldr  r0, [r0]

    tst  r0, #0x8000  /* BFARVALID */
    beq  _ExcBusNoADDR
    ldr  r1, =OS_NVIC_BFAR
    ldr  r1, [r1]
    mov  r12, #FLAG_ADDR_VALID
    and  r0, r0, #0x1F00

    b    osExcCommonBMU
    FNEND

    FUNC _ExcBusNoADDR
_ExcBusNoADDR:
    FNSTART
    CANTUNWIND
    mov  r12,#0
    b    osExcCommonBMU
    FNEND

    FUNC HalExcMemFault
HalExcMemFault:
    FNSTART
    CANTUNWIND
    ldr  r0, =OS_NVIC_FSR
    ldr  r0, [r0]

    tst  r0, #0x80 /* MMARVALID */
    beq  _ExcMemNoADDR
    ldr  r1, =OS_NVIC_MMAR
    ldr  r1, [r1]
    mov  r12, #FLAG_ADDR_VALID
    and  r0, r0, #0x1B

    b    osExcCommonBMU
    FNEND

    FUNC _ExcMemNoADDR
_ExcMemNoADDR:
    FNSTART
    CANTUNWIND
    mov  r12,#0
    b    osExcCommonBMU
    FNEND

    FUNC HalExcUsageFault
HalExcUsageFault:
    FNSTART
    CANTUNWIND
    ldr  r0, =OS_NVIC_FSR
    ldr  r0, [r0]

    movw r1, #0x030F
    lsl  r1, r1, #16
    and  r0, r0, r1
    mov  r12, #0

    FNEND

    FUNC osExcCommonBMU
osExcCommonBMU:
    FNSTART
    CANTUNWIND
    clz  r0, r0
    ldr  r3, =g_uwExcTbl
    add  r3, r3, r0
    ldrb r0, [r3]
    orr  r0, r0, r12
    FNEND
/* r0 -- EXCCAUSE(bit 16 is 1 if EXCADDR valid),  r1 -- EXCADDR */
#endif /* !LOSCFG_ARCH_ARM_V6M */

#ifdef LOSCFG_ARCH_ARM_V6M
    /* ARMv6-M has no separate MemManage/Bus/Usage faults (all escalate to
     * HardFault); these stubs only satisfy the shared NVIC driver, which
     * installs them unconditionally — the M0+ slots are Reserved. */
    FUNC HalExcMemFault
HalExcMemFault:
    FNSTART
    CANTUNWIND
    b       HalExcHardFault
    FNEND

    FUNC HalExcBusFault
HalExcBusFault:
    FNSTART
    CANTUNWIND
    b       HalExcHardFault
    FNEND

    FUNC HalExcUsageFault
HalExcUsageFault:
    FNSTART
    CANTUNWIND
    b       HalExcHardFault
    FNEND

    FUNC HalExcSvcCall
HalExcSvcCall:
    FNSTART
    CANTUNWIND
    mrs     r1, psp
    ldr     r0, [r1, #24]
    subs    r0, r0, #2
    ldrb    r0, [r0]
    movs    r1, #0
    b       osExcDispatch
    FNEND
#endif /* LOSCFG_ARCH_ARM_V6M */

    FUNC osExcDispatch
osExcDispatch:
    FNSTART
    CANTUNWIND

#ifdef LOSCFG_ARCH_ARM_V6M
    /* EXC_RETURN bit 2 selects the fault context: 0 = msp, 1 = psp (task).
     * r2/r3 are safe to clobber: task values live in the hardware frame. */
    mov     r2, lr
    movs    r3, #4
    tst     r2, r3
    bne     osExcFromTask

    /* Fault from msp (handler mode): frame already contiguous after our
     * 40-byte save area — no copy needed. */
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

    /* r4-r7 now free — check NVIC active registers for EXTERNAL IRQ */
    ldr     r2, =OS_NVIC_ACT_BASE
    movs    r4, #0
osExcIrqLoop:
    ldr     r5, [r2, r4]
    cmp     r5, #0
    bne     osExcInIrq
    adds    r4, r4, #4
    cmp     r4, #32
    bne     osExcIrqLoop

    /* Not in EXTERNAL IRQ — check if scheduler started */
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

    /* Fault from psp (task): copy the frame to msp for a
     * contiguous EXC_CONTEXT_S layout. */
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

    /* Copy the 8-word hardware frame from psp (r3) to sp+40 so that
     * EXC_CONTEXT_S is contiguous on msp. */
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
    FNEND
#else
    ldr   r2, =OS_NVIC_ACT_BASE
    mov   r12, #8                       /* r12 is hwi check loop counter */
    FNEND

    FUNC _hwiActiveCheck
_hwiActiveCheck:
    FNSTART
    CANTUNWIND
    ldr   r3, [r2]                      /* r3 store active hwi register when exc */
    cmp   r3, #0
    beq   _hwiActiveCheckNext

    /* exc occurred in IRQ */
    orr   r0, r0, #FLAG_HWI_ACTIVE
    rbit  r2, r3
    clz   r2, r2
    and   r12, r12, #1
    add   r2, r2, r12, LSL #5               /* calculate r2 (hwi number) as pid */
    FNEND

    FUNC _ExcInMSP
_ExcInMSP:
    FNSTART
    CANTUNWIND
    cmp   lr, #0xFFFFFFE9
    bne   _NoFloatInMsp
    add   r3, r13, #104
    push  {r3}
    mrs   r12, primask                  /* store message-->exc: disable int? */
    push {r4-r12}                       /* store message-->exc: {r4-r12} */
    vpush {d8-d15}                      /* FPU */
    b     _handleEntry
    FNEND

    FUNC _NoFloatInMsp
_NoFloatInMsp:
    FNSTART
    CANTUNWIND
    add   r3, r13, #32
    push  {r3}                           /* store message-->exc: msp(r13) */

    mrs   r12, primask                   /* store message-->exc: disable int? */
    push {r4-r12}                        /* store message-->exc: {r4-r12} */
    orr   r0, r0, #FLAG_NO_FLOAT
    b     _handleEntry
    FNEND

    FUNC _hwiActiveCheckNext
_hwiActiveCheckNext:
    FNSTART
    CANTUNWIND
    add   r2, r2, #4                        /* next NVIC ACT ADDR */
    subs  r12, r12, #1
    bne   _hwiActiveCheck

    /* NMI interrupt excption */
    ldr   r2, =OS_NVIC_SHCSRS
    ldrh  r2,[r2]
    ldr   r3,=OS_NVIC_SHCSR_MASK
    and   r2, r2,r3
    cmp   r2,#0
    bne   _ExcInMSP
    /* exc occurred in Task or Init or exc */
    /* reserved for register info from task stack */

    ldr  r2, =g_taskScheduled
    ldr  r2, [r2]
    tst  r2, #OS_FLG_BGD_ACTIVE
    beq  _ExcInMSP                      /* if exc occurred in Init then branch */

    cmp   lr, #0xFFFFFFED               /* auto push floating registers */
    bne   _NoFloatInPsp

    /* exc occurred in Task */
    mov   r2,  sp
    sub   sp, #96                       /* add 8 Bytes reg(for STMFD) */

    mrs   r3,  psp
    add   r12, r3, #104
    push  {r12}                         /* save task sp */

    mrs   r12, primask
    push {r4-r12}
    vpush {d8-d15}                      /* FPU */

    /* copy auto saved task register */

    ldmfd r3!, {r4-r11}                 /* r4-r11 store psp reg(auto push when exc in task) */
    vldmia  r3!, {d8-d15}               /* FPU */
    vstmdb  r2!, {d8-d15}               /* FPU */
    stmfd r2!, {r4-r11}
    b     _handleEntry
    FNEND

    FUNC _NoFloatInPsp
_NoFloatInPsp:
    FNSTART
    CANTUNWIND
    mov   r2,  sp                      /* no auto push floating registers */
    sub   sp, #32                      /* add 8 Bytes reg(for STMFD) */

    mrs   r3,  psp
    add   r12, r3, #32
    push  {r12}                         /* save task sp */

    mrs   r12, primask
    push {r4-r12}

    ldmfd r3, {r4-r11}                  /* r4-r11 store psp reg(auto push when exc in task) */
    stmfd r2!, {r4-r11}
    orr   r0, r0, #FLAG_NO_FLOAT
    FNEND

    FUNC _handleEntry
_handleEntry:
    FNSTART
    CANTUNWIND
    mov r3, sp                         /* sp:the 4th param */
    cpsid I
    cpsid F
    b  HalExcHandleEntry

    nop
    FNEND
#endif /* LOSCFG_ARCH_ARM_V6M */

#ifdef __IASMARM__
    END
#endif
