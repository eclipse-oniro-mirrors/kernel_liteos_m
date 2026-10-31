#include "arch/macro.h"
#ifdef __IASMARM__
    PRESERVE8
    SECTION    .text:CODE(2)
    THUMB
#else
    .syntax unified
    .arch armv7-m
    .thumb
    .fpu  vfpv4
    .section .text
#endif

    EXTERNAL   OsExcHandleEntry
    EXTERNAL   g_excTbl
    EXTERNAL   g_taskScheduled
#ifdef LOSCFG_EXC_ISOLATION
    EXTERNAL   g_excSaveMsp
#endif
    EXTERNAL   OsExcSvcCall
    EXTERNAL   g_sysMode

    EQUAL OS_EXC_CAUSE_NMI               , 16
    EQUAL OS_EXC_CAUSE_HARDFAULT         , 17
    EQUAL HF_DEBUGEVT                    , 20
    EQUAL HF_VECTBL                      , 21

    EQUAL OS_EXC_FLAG_FAULTADDR_VALID    , 0x10000 
    EQUAL OS_EXC_FLAG_IN_HWI             , 0x20000
    EQUAL OS_EXC_FLAG_NO_FLOAT           , 0x10000000

    EQUAL OS_NVIC_FSR                    , 0xE000ED28      /* include BusFault/MemFault/UsageFault State Register */
    EQUAL OS_NVIC_HFSR                   , 0xE000ED2C      /* HardFault State Register */
    EQUAL OS_NVIC_BFAR                   , 0xE000ED38
    EQUAL OS_NVIC_MMAR                   , 0xE000ED34
    EQUAL OS_NVIC_ACT_BASE               , 0xE000E300
    EQUAL OS_NVIC_SHCSRS                 , 0xE000ED24
    EQUAL OS_NVIC_SHCSR_MASK             , 0xC00
    EQUAL EXCEPT_FRAME_OFFSET_PC         , 0x06 * 4      /* see cortex-m7 reference manual: chapter 2.3.7 */
    EQUAL SVC_INS                        , 0xdf          /* SVCall instruction 0xdf */

    FUNC OsExcNMI
OsExcNMI:
    FNSTART
    CANTUNWIND
    mov  r0, #OS_EXC_CAUSE_NMI
    mov  r1, #0
    b    OsExcDispatch
    FNEND

    FUNC OsExcHardFault
OsExcHardFault:
    FNSTART
    CANTUNWIND

    /* Check if it is SVCall while primask = 1 */
    mrs  r0, psp
    ldr  r1, =OsExcSvcCall
    ldr  r0, [r0, #24] /* Get pc */
    ldrb r0, [r0, #-1] /* Get SVCall instruction */
    cmp  r0, #SVC_INS
    it   eq
    bxeq r1

    mov  r0, #OS_EXC_CAUSE_HARDFAULT
    ldr  r2, =OS_NVIC_HFSR
    ldr  r2, [r2]

    mov  r1, #HF_DEBUGEVT
    orr  r0, r0, r1, lsl #0x8
    tst  r2, #0x80000000
    bne  OsExcDispatch     /* DEBUGEVT */

    and  r0, #0x000000FF
    mov  r1, #HF_VECTBL
    orr  r0, r0, r1, lsl #0x8
    tst  r2, #0x00000002
    bne  OsExcDispatch     /* VECTBL */

    /* if not DEBUGEVT and VECTBL then is FORCED */
    and  r0, #0x000000FF

    mrs  r2, msp
    ldr  r1, [r2, #EXCEPT_FRAME_OFFSET_PC]

    ldr  r2, =OS_NVIC_FSR
    ldr  r2, [r2]

    tst  r2, #0x8000     /* BFARVALID */
    bne  HFBusFault      /* BusFault */

    tst  r2, #0x80       /* MMARVALID */
    bne  HFMemFault      /* MemFault */

    mov  r12,#0
    b    OsHFExcCommonBMU
    FNEND

HFBusFault:
    FNSTART
    CANTUNWIND
    ldr  r1, =OS_NVIC_BFAR
    ldr  r1, [r1]
    mov  r12, #OS_EXC_FLAG_FAULTADDR_VALID
    b    OsHFExcCommonBMU
    FNEND

HFMemFault:
    FNSTART
    CANTUNWIND
    ldr  r1, =OS_NVIC_MMAR
    ldr  r1, [r1]
    mov  r12, #OS_EXC_FLAG_FAULTADDR_VALID
    FNEND

    FUNC OsHFExcCommonBMU
OsHFExcCommonBMU:
    FNSTART
    CANTUNWIND
    clz  r2, r2
    ldr  r3, =g_excTbl
    add  r3, r3, r2
    ldrb r2, [r3]
    orr  r0, r0, r2, lsl #0x8
    orr  r0, r12
    b    OsExcDispatch
    FNEND

    FUNC OsExcBusFault
OsExcBusFault:
    FNSTART
    CANTUNWIND
    ldr  r0, =OS_NVIC_FSR
    ldr  r0, [r0]

    tst  r0, #0x8000  /* BFARVALID */
    beq  ExcBusNoAddr
    ldr  r1, =OS_NVIC_BFAR
    ldr  r1, [r1]
    mov  r12, #OS_EXC_FLAG_FAULTADDR_VALID
    and  r0, #0x1F00

    b    OsExcCommonBMU
    FNEND

ExcBusNoAddr:
    FNSTART
    CANTUNWIND
    mov  r12, #0
    b    OsExcCommonBMU
    FNEND

    FUNC OsExcMemFault
OsExcMemFault:
    FNSTART
    CANTUNWIND
    ldr  r0, =OS_NVIC_FSR
    ldr  r0, [r0]

    tst  r0, #0x80 /* MMARVALID */
    beq  ExcMemNoAddr
    ldr  r1, =OS_NVIC_MMAR
    ldr  r1, [r1]
    mov  r12, #OS_EXC_FLAG_FAULTADDR_VALID
    and  r0, #0x1B

    b    OsExcCommonBMU
    FNEND

ExcMemNoAddr:
    FNSTART
    CANTUNWIND
    mov  r12,#0
    b    OsExcCommonBMU
    FNEND

    FUNC OsExcUsageFault
OsExcUsageFault:
    ldr  r0, =OS_NVIC_FSR
    ldr  r0, [r0]

    ldr  r1, =0x030F
    lsl  r1, #16
    and  r0, r1
    mov  r12, #0

OsExcCommonBMU:
    clz  r0, r0
    ldr  r3, =g_excTbl
    add  r3, r3, r0
    ldrb r0, [r3]
    orr  r0, r0, r12

/* r0 -- EXCCAUSE(bit 16 is 1 if EXCADDR valid),  r1 -- EXCADDR */
OsExcDispatch:
#ifdef LOSCFG_EXC_ISOLATION
    ldr  r2, =g_excSaveMsp
    mov  r3, sp
    str  r3, [r2]
#endif
    ldr  r2, =g_sysMode
    mov  r3, #0
    str  r3, [r2]
    ldr  r2, =OS_NVIC_ACT_BASE
    mov  r12, #8 /* r12 is hwi check loop counter */

HwiActiveCheck:
    ldr   r3, [r2]                      /* r3 store active hwi register when exc */
    cmp   r3, #0
    beq   HwiActiveCheckNext

    /* exc occurred in IRQ */
    orr   r0, #OS_EXC_FLAG_IN_HWI
    rbit  r2, r3
    clz   r2, r2
    and   r12, #1
    add   r2, r2, r12, lsl #5               /* calculate r2 (hwi number) as pid, third parameter */

ExcInMSP:
    cmp   lr, #0xFFFFFFED
    bne   NoFloatInMsp
    add   r3, r13, #104               /* hardware auto save regs is 104 bytes */
    push  {r3}
    mrs   r12, primask                  /* store message-->exc: disable int */
    push  {r4-r12}                      /* store message-->exc: {r4-r12} */
    vpush {d8-d15}                      /* FPU */
    b     HandleEntry

NoFloatInMsp:
    add   r3, r13, #32                   /* hardware auto save regs is 32 bytes */
    push  {r3}                           /* store message-->exc: msp(r13) */

    mrs   r12, primask                   /* store message-->exc: disable int? */
    push  {r4-r12}                       /* store message-->exc: {r4-r12} */
    orr   r0, r0, #OS_EXC_FLAG_NO_FLOAT
    b     HandleEntry

HwiActiveCheckNext:
    add   r2, #4                        /* next NVIC ACT ADDR */
    subs  r12, #1
    bne   HwiActiveCheck

    /* NMI interrupt excption */
    ldr   r2, =OS_NVIC_SHCSRS
    ldrh  r2,[r2]
    ldr   r3,=OS_NVIC_SHCSR_MASK
    and   r2, r2,r3
    cmp   r2,#0
    bne   ExcInMSP

    /* exc occurred in Task or Init or exc reserved for register info from task stack */
    ldr  r2, =g_taskScheduled
    ldr  r2, [r2]
    tst  r2, #1                         /* os scheduled */
    beq  ExcInMSP                       /* if exc occurred in Init then branch */

    cmp   lr, #0xFFFFFFED               /* auto push floating registers */
    bne   NoFloatInPsp

    /* exc occurred in Task */
    mov   r2,  r13
    sub   r13, #96                      /* add 8 Bytes reg(for srmfd) */

    mrs   r3,  psp
    add   r12, r3, #104
    push  {r12}                         /* save task sp */

    mrs   r12, primask
    push  {r4-r12}
    vpush {d8-d15}                      /* FPU */

    /* copy auto saved task register */
    ldmfd   r3!, {r4-r11}               /* r4-r11 store psp reg(auto push when exc in task) */
    vldmia  r3!, {d8-d15}
    vstmdb  r2!, {d8-d15}
    stmfd r2!, {r4-r11}
    b     HandleEntry

NoFloatInPsp:
#ifdef LOSCFG_ARCH_FPU_ENABLE
    sub   r13, #72                      /* reserved for not float S0 ~ FPSCR */
#endif
    mov   r2,  r13                      /* no auto push floating registers */
    sub   r13, #32                      /* add 8 Bytes reg(for stmfd) */

    mrs   r3,  psp
    add   r12, r3, #32
    push  {r12}                         /* save task sp */

    mrs   r12, primask
    push {r4-r12}

    ldmfd r3, {r4-r11}                  /* r4-r11 store psp reg(auto push when exc in task) */
    stmfd r2!, {r4-r11}
    orr   r0, r0, #OS_EXC_FLAG_NO_FLOAT

HandleEntry:
    mov r3, r13                         /* r13:the 4th param */
    cpsid i
    cpsid f
    b  OsExcHandleEntry

    nop
#ifdef __IASMARM__
    END
#endif