/*
 * Copyright (c) 2026 morpheus
 * AArch64 task dispatch and IRQ entry. Ported from hs-fbb
 * arch_lite/arm64/src/dispatch.S, adapted to the liteos_m contract:
 *   VOID ArchStartToRun(LosTaskCB *newTask);
 *   VOID ArchTaskSchedule(LosTaskCB *newTask, LosTaskCB *runTask);
 *   _osExceptIrqHdl: IRQ vector entry -> HalInterrupt (GICv3 ICC_IAR1_EL1).
 *
 * LosTaskCB->stackPointer is the first struct member, so [x0]/[x1]
 * access it directly.
 */

    .arch armv8-a

    .extern HalInterrupt
    .extern OsSchedProcSchedFlag

#define fp  x29
#define lr  x30

/* EL1 execution level bit for SPSR (EL1h): mode bits [3:0] = 0b0101 */
#define RUNLVL      4

    .global _osExceptIrqHdl
    .global ArchStartToRun
    .global ArchTaskSchedule
    .global ArchIntLock
    .global ArchIntUnLock
    .global ArchIntRestore

/* ---- interrupt lock/unlock (DAIF), cortex_a_r-compatible contract ---- */
    .align  5
ArchIntLock:
    mrs     x0, daif
    msr     daifset, #3
    ret

    .align  5
ArchIntUnLock:
    mrs     x0, daif
    msr     daifclr, #3
    ret

    .align  5
ArchIntRestore:
    msr     daif, x0
    ret

    .macro  PUSH reg1, reg2
    stp     \reg1, \reg2, [sp, #-16]!
    .endm

    .macro  POP reg1, reg2
    ldp     \reg1, \reg2, [sp], #16
    .endm

/* FPU pairs are stored with 32-byte stride so Q regs stay 16-byte aligned
 * in the saved frame (matches TaskContext layout in los_arch_context.h). */
    .macro  FPU_PUSH ra, rb
    stp     \ra, \rb, [sp, #-32]!
    .endm

    .macro  FPU_POP ra, rb
    ldp     \ra, \rb, [sp], #32
    .endm

    .macro  PUSH_REGS
    PUSH    x28, x29
    PUSH    x26, x27
    PUSH    x24, x25
    PUSH    x22, x23
    PUSH    x20, x21
    PUSH    x18, x19
    PUSH    x16, x17
    PUSH    x14, x15
    PUSH    x12, x13
    PUSH    x10, x11
    PUSH    x8,  x9
    PUSH    x6,  x7
    PUSH    x4,  x5
    PUSH    x2,  x3
    PUSH    x0,  x1
    .endm

    .macro  POP_REGS
    POP     x0,  x1
    POP     x2,  x3
    POP     x4,  x5
    POP     x6,  x7
    POP     x8,  x9
    POP     x10, x11
    POP     x12, x13
    POP     x14, x15
    POP     x16, x17
    POP     x18, x19
    POP     x20, x21
    POP     x22, x23
    POP     x24, x25
    POP     x26, x27
    POP     x28, x29
    .endm

/* Task context save (ArchTaskSchedule). Frame:
 *   X0-X29 (15*16=240), LR @+240, DAIF/NZCV @+256, 32B pad below. */
    .macro  PUSH_AARCH64
    sub     sp, sp, #32
    PUSH_REGS
    mrs     x2, daif
    mrs     x3, nzcv
    stp     lr, lr, [sp, #(15 * 16)]
    stp     x2, x3, [sp, #(15 * 16 + 16)]
    .endm

    .macro  POP_AARCH64
    ldp     x1, x2, [sp, #16 * 16]
    orr     x1, x1, x2
    orr     x1, x1, #1
    orr     x1, x1, #RUNLVL      /* mode bits: EL1h */
    msr     spsr_el1, x1
    POP_REGS
    ldp     lr, x1, [sp], #16
    add     sp, sp, #16
    .endm

/* IRQ frame: X0-X19 (10*16=160), NZCV/DAIF @+160, LR/ELR @+176. */
    .macro  IRQ_PUSH_REGS
    sub     sp, sp, #32
    PUSH    x18, x19
    PUSH    x16, x17
    PUSH    x14, x15
    PUSH    x12, x13
    PUSH    x10, x11
    PUSH    x8,  x9
    PUSH    x6,  x7
    PUSH    x4,  x5
    PUSH    x2,  x3
    PUSH    x0,  x1
    mrs     x3, spsr_el1
    and     x0, x3, #0xf0000000 /* NZCV */
    and     x1, x3, #0x3c0      /* DAIF */
    mrs     x2, elr_el1
    stp     x0, x1, [sp, #(10 * 16)]
    stp     lr, x2, [sp, #(10 * 16 + 16)]
    .endm

    .macro  IRQ_POP_REGS
    ldp     lr, x2, [sp, #(10 * 16 + 16)]
    ldp     x0, x1, [sp, #(10 * 16)]
    msr     elr_el1, x2
    orr     x0, x0, x1
    orr     x0, x0, #1
    orr     x0, x0, #RUNLVL      /* mode bits: EL1h */
    msr     spsr_el1, x0
    POP     x0, x1
    POP     x2, x3
    POP     x4, x5
    POP     x6, x7
    POP     x8, x9
    POP     x10, x11
    POP     x12, x13
    POP     x14, x15
    POP     x16, x17
    POP     x18, x19
    add     sp, sp, #32
    .endm

/* FPU/SIMD context. */
    .macro  FPU_PUSH_REGS
    mrs     x2, fpcr
    mrs     x3, fpsr
    stp     x2, x3, [sp, #-16]!
    FPU_PUSH q30, q31
    FPU_PUSH q28, q29
    FPU_PUSH q26, q27
    FPU_PUSH q24, q25
    FPU_PUSH q22, q23
    FPU_PUSH q20, q21
    FPU_PUSH q18, q19
    FPU_PUSH q16, q17
    FPU_PUSH q14, q15
    FPU_PUSH q12, q13
    FPU_PUSH q10, q11
    FPU_PUSH q8,  q9
    FPU_PUSH q6,  q7
    FPU_PUSH q4,  q5
    FPU_PUSH q2,  q3
    FPU_PUSH q0,  q1
    .endm

    .macro  FPU_POP_REGS
    FPU_POP q0,  q1
    FPU_POP q2,  q3
    FPU_POP q4,  q5
    FPU_POP q6,  q7
    FPU_POP q8,  q9
    FPU_POP q10, q11
    FPU_POP q12, q13
    FPU_POP q14, q15
    FPU_POP q16, q17
    FPU_POP q18, q19
    FPU_POP q20, q21
    FPU_POP q22, q23
    FPU_POP q24, q25
    FPU_POP q26, q27
    FPU_POP q28, q29
    FPU_POP q30, q31
    ldp     x2, x3, [sp], #16
    msr     fpsr, x3
    msr     fpcr, x2
    .endm

/*
 * x0: pstNewTask
 */
    .align  5
ArchStartToRun:
    msr     daifset, #3

    /* x0 is new task, save it on tpidr_el1 */
    msr     tpidr_el1, x0
    isb

#ifdef LOSCFG_ARCH_FPU_ENABLE
    /* touch FPU so a lazy trap (if any) fires here, not inside the task */
    stp     q0, q1, [sp, #-32]!
    ldp     q0, q1, [sp], #32
#endif

    b       OsTaskContextLoad

/*
 * x0: pstNewTask
 * x1: pstRunTask
 */
    .align  5
ArchTaskSchedule:
    PUSH_AARCH64

#ifdef LOSCFG_ARCH_FPU_ENABLE
    FPU_PUSH_REGS
#endif

    msr     daifset, #3

    /* store sp on running task */
    mov     x2, sp
    str     x2, [x1]

OsTaskContextLoad:
    /* clear the flag of ldrex */
    clrex

    /* switch to new task's sp */
    ldr     x2, [x0]
    mov     sp, x2

osThrdContextLoad:
#ifdef LOSCFG_ARCH_FPU_ENABLE
    FPU_POP_REGS
#endif

    POP_AARCH64

    /* x0 is parameter, use scratch x1 */
    ret     x1

/* IRQ exception entry (from vector table, current EL SPx). */
    .align  5
_osExceptIrqHdl:
    msr     daifset, #3

    /* save irq context on the current (task) stack */
    IRQ_PUSH_REGS
#ifdef LOSCFG_ARCH_FPU_ENABLE
    FPU_PUSH_REGS
#endif

#ifdef LOSCFG_IRQ_USE_STANDALONE_STACK
    /* switch to the dedicated IRQ stack; x19 holds the task SP across
     * the C call (callee-saved, and reloaded from the frame by IRQ_POP) */
    mov     x19, sp
    ldr     x9, =__irq_stack_top
    mov     sp, x9
#endif

    /* interrupt entry: ICC_IAR1_EL1 -> dispatch -> ICC_EOIR1_EL1 */
    bl      HalInterrupt

#ifdef LOSCFG_IRQ_USE_STANDALONE_STACK
    mov     sp, x19
#endif

    /* process pending signals and resched */
    bl      OsSchedProcSchedFlag

#ifdef LOSCFG_ARCH_FPU_ENABLE
    FPU_POP_REGS
#endif
    IRQ_POP_REGS
    eret

    .end
