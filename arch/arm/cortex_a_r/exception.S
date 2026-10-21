/*
 * Copyright (c) 2024 morpheus
 */

.syntax unified
.arch armv7-a
.arm

.extern HalExcHandleEntry
.extern main
.extern OsMmuInit
.extern g_curNestCount
.extern __bss_start
.extern __bss_end

    .section .vectors, "ax"
    .align 5
    .global __exception_handlers
    .global reset_vector
    .global _start
_start:
__exception_handlers:
    b   reset_vector               /* 0x00: reset */
    b   HalExcUndefInstr           /* 0x04: undefined instruction */
    b   HalExcSvcCall              /* 0x08: SVC */
    b   HalExcPrefAbort            /* 0x0C: prefetch abort */
    b   HalExcDataAbort            /* 0x10: data abort */
    b   .                         /* 0x14: not used */
    b   OsIrqHandler               /* 0x18: IRQ */
    b   HalExcFiq                  /* 0x1C: FIQ */

reset_vector:

    mov     r0, #0
    mcr     p15, 0, r0, c13, c0, 4   /* clear TPIDRPRW (percpu) */
    mrc     p15, 0, r0, c1, c0, 0    /* read SCTLR */
    bic     r0, r0, #(1 << 12)      /* ICache off */
    bic     r0, r0, #(1 << 2)       /* DCache off */
    bic     r0, r0, #(1 << 0)       /* MMU off */
    mcr     p15, 0, r0, c1, c0, 0   /* write SCTLR */
    isb

    /* invalidate ICache / BTB / TLB */
    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0   /* invalidate ICache (all) */
    mcr     p15, 0, r0, c7, c5, 6   /* invalidate BTB */
    mcr     p15, 0, r0, c8, c7, 0   /* invalidate TLB (Unified) */
    dsb
    isb

    ldr     r0, =__exception_handlers
    mcr     p15, 0, r0, c12, c0, 0  /* VBAR */
    isb

    /* read CPU ID (MPIDR[7:0]) */
    mrc     p15, 0, r0, c0, c0, 5   /* MPIDR */
    and     r0, r0, #0x03
    cmp     r0, #1
    beq     core1_wfi

    /* mode stack init, stacks defined in board.ld (.stack section) */
    msr     cpsr_c, #0xD2              /* IRQ mode */
    ldr     sp, =__irq_stack_top
    msr     cpsr_c, #0xDB              /* UND mode */
    ldr     sp, =__und_stack_top
    msr     cpsr_c, #0xD7              /* ABT mode */
    ldr     sp, =__abt_stack_top
    msr     cpsr_c, #0xD1              /* FIQ mode */
    ldr     sp, =__fiq_stack_top
    msr     cpsr_c, #0xD3              /* SVC mode, IRQ/FIQ off */
    ldr     sp, =__svc_stack_top

    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:
    cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    blx     OsMmuInit

    /* jump to C main */
    blx     main

hang:
    b       hang

core1_wfi:

    msr     cpsr_c, #0xD3              /* SVC mode, IRQ off */
wfi_loop:
    wfi
    b       wfi_loop

/*
 * Exception entry: R0-R7 scratch-saved on the faulting mode's own stack,
 * then the full frame is built on the dedicated exception stack in SVC
 * mode. R0 = excType, R4 = faultAddr on entry to HalExcDispatch.
 */
    .section .text
    .align 5

    .global HalExcUndefInstr
    .type   HalExcUndefInstr, %function
HalExcUndefInstr:
    sub     lr, lr, #4             /* faulting PC */
    STMFD   SP, {R0-R7}
    mov     r0, #0                 /* excType: UNDEF */
    mov     r4, lr                 /* faultAddr = faulting PC */
    b       HalExcDispatch

    .global HalExcPrefAbort
    .type   HalExcPrefAbort, %function
HalExcPrefAbort:
    sub     lr, lr, #4             /* faulting PC */
    STMFD   SP, {R0-R7}
    movw    r0, #0x0001            /* excType: PABT(1) */
    movt    r0, #0x0001            /* + addr valid flag(0x10000) */
    mov     r4, lr                 /* faultAddr = faulting PC */
    b       HalExcDispatch

    .global HalExcDataAbort
    .type   HalExcDataAbort, %function
HalExcDataAbort:
    sub     lr, lr, #8             /* faulting PC */
    STMFD   SP, {R0-R7}
    mrc     p15, 0, r4, c6, c0, 0  /* faultAddr = FAR */
    movw    r0, #0x0002            /* excType: DABT(2) */
    movt    r0, #0x0001            /* + addr valid flag(0x10000) */
    b       HalExcDispatch

    .global HalExcSvcCall
    .type   HalExcSvcCall, %function
HalExcSvcCall:

    movs    pc, lr

    .global HalExcFiq
    .type   HalExcFiq, %function
HalExcFiq:
    sub     lr, lr, #4             /* faulting PC */
    STMFD   SP, {R0-R7}
    mov     r0, #0                 /* excType: FIQ */
    mov     r4, lr                 /* faultAddr = faulting PC */
    b       HalExcDispatch

/*
 * Build EXC_CONTEXT_S (17 words + 1 parked word, 72 bytes) on the
 * exception stack. First-level faults run the C dump on the svc stack,
 * nested faults stay on the exception stack below the new frame.
 * HalExcHandleEntry never returns.
 */
HalExcDispatch:
    mrs     r1, spsr               /* pre-exception CPSR */
    mov     r2, lr                 /* faulting PC */
    sub     r3, sp, #(8 * 4)       /* saved R0-R7 */
    msr     cpsr_c, #0xD3          /* SVC mode, IRQ/FIQ off */
    mov     r5, sp                 /* task SP */
    ldr     sp, =__exc_stack_top
    STMFD   SP!, {R4}              /* faultAddr, keeps 8-byte align */
    STMFD   SP!, {R1}              /* uwxPSR */
    STMFD   SP!, {R2}              /* uwPC */
    STMFD   SP!, {LR}              /* uwLR */
    STMFD   SP!, {R5}              /* uwSP */
    STMFD   SP!, {R8-R12}
    LDMFD   R3!, {R4-R11}          /* original R0-R7 */
    STMFD   SP!, {R4-R11}
    mov     r6, sp                 /* r6 = ExcContext */
    ldr     r7, =g_curNestCount
    ldr     r7, [r7]
    cmp     r7, #0
    bne     HalExcCall             /* nested: run C on exc stack */
    ldr     sp, =__svc_stack_top   /* first-level: run C on svc stack */
HalExcCall:
    mov     r3, r6
    mov     r2, #0                 /* pid */
    ldr     r1, [r6, #68]          /* faultAddr */
    blx     HalExcHandleEntry
    b       .
