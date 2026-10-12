/*
 * Copyright (c) 2026 morpheus
 * AArch64 exception vector table (VBAR_EL1, 16-slot layout) and sync
 * exception entry. Ported from hs-fbb arch_lite/arm64/src/exception.S,
 * adapted to the liteos_m exception contract (HalExcHandleEntry).
 *
 * Vector layout (0x80 bytes per slot, 0x200 total):
 *   0x000 cur EL SP0  sync   0x200 lower EL AArch64 sync
 *   0x080 cur EL SP0  irq    0x280 lower EL AArch64 irq
 *   0x100 cur EL SP0  fiq    0x300 lower EL AArch64 fiq
 *   0x180 cur EL SP0  serror 0x380 lower EL AArch64 serror
 *   0x400 cur EL SPx  sync   0x600 lower EL AArch32 sync
 *   0x480 cur EL SPx  irq    0x680 lower EL AArch32 irq
 *   0x500 cur EL SPx  fiq    0x700 lower EL AArch32 fiq
 *   0x580 cur EL SPx  serror 0x780 lower EL AArch32 serror
 */

    .arch armv8-a

    .extern OsArm64SyncExcHandler
    .extern _osExceptIrqHdl

#define ALIGN .align

    LR  .req    x30

/* Save the integer exception frame on the current SP.
 * Layout must match EXC_CONTEXT_S in los_arch_interrupt.h:
 *   X0-X29 (0x000-0x0EF), LR (0x0F0), scratch (0x0F8),
 *   ELR_EL1 (0x100), SPSR_EL1 (0x108). Total 0x110 bytes. */
    .macro  push_all
    sub     sp, sp, #32
    stp     x28, x29, [sp, #-16]!
    stp     x26, x27, [sp, #-16]!
    stp     x24, x25, [sp, #-16]!
    stp     x22, x23, [sp, #-16]!
    stp     x20, x21, [sp, #-16]!
    stp     x18, x19, [sp, #-16]!
    stp     x16, x17, [sp, #-16]!
    stp     x14, x15, [sp, #-16]!
    stp     x12, x13, [sp, #-16]!
    stp     x10, x11, [sp, #-16]!
    stp     x8, x9, [sp, #-16]!
    stp     x6, x7, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x0, x1, [sp, #-16]!
    mrs     x1, elr_el1
    mrs     x2, spsr_el1
    stp     lr, xzr, [sp, #0xf0]
    stp     x1, x2, [sp, #0xf0 + 16]
    .endm

    .macro  pop_all
    ldp     x1, x2, [sp, #0xf0 + 16]
    msr     elr_el1, x1
    msr     spsr_el1, x2
    ldp     lr, xzr, [sp, #0xf0]
    ldp     x0, x1, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x4, x5, [sp], #16
    ldp     x6, x7, [sp], #16
    ldp     x8, x9, [sp], #16
    ldp     x10, x11, [sp], #16
    ldp     x12, x13, [sp], #16
    ldp     x14, x15, [sp], #16
    ldp     x16, x17, [sp], #16
    ldp     x18, x19, [sp], #16
    ldp     x20, x21, [sp], #16
    ldp     x22, x23, [sp], #16
    ldp     x24, x25, [sp], #16
    ldp     x26, x27, [sp], #16
    ldp     x28, x29, [sp], #16
    add     sp, sp, #32
    .endm

/* Slot we never expect to take: park the core. */
    .macro  exc_hang slot
\slot:
    msr     daifset, #0xf
    b       .
    .endm

/* ---------------------------------------------------------------
 * Vector table: placed in .vectors (2KB-aligned, see board.ld).
 * --------------------------------------------------------------- */
    .section .vectors, "ax"
    .align  11
    .global arm64_exception_handlers
    .type   arm64_exception_handlers, %function
arm64_exception_handlers:
    /* 0x000: current EL, SP0, sync */
    exc_hang arm64_exc_cur_sp0_sync
    .org    0x080
    /* 0x080: current EL, SP0, irq */
    exc_hang arm64_exc_cur_sp0_irq
    .org    0x100
    /* 0x100: current EL, SP0, fiq */
    exc_hang arm64_exc_cur_sp0_fiq
    .org    0x180
    /* 0x180: current EL, SP0, serror */
    exc_hang arm64_exc_cur_sp0_serror
    .org    0x200
    /* 0x200: current EL, SPx, sync */
    .type   arm64_sync_exc_cur_spx, %function
arm64_sync_exc_cur_spx:
    push_all
    msr     daifset, #0xf
    mov     x0, sp
    bl      OsArm64SyncExcHandler
    /* never returns; fall back to restore in case it does */
    b       arm64_exc_pop_all
    .org    0x280
    /* 0x280: current EL, SPx, irq */
    .type   arm64_irq_cur_spx, %function
arm64_irq_cur_spx:
    b       _osExceptIrqHdl
    .org    0x300
    /* 0x300: current EL, SPx, fiq */
    exc_hang arm64_exc_cur_spx_fiq
    .org    0x380
    /* 0x380: current EL, SPx, serror */
    .type   arm64_serror_cur_spx, %function
    exc_hang arm64_serror_cur_spx_1
arm64_serror_cur_spx:
    push_all
    msr     daifset, #0xf
    mov     x0, sp
    bl      OsArm64SyncExcHandler
    b       arm64_exc_pop_all
    .org    0x400
    /* 0x400: lower EL, AArch64, sync */
    exc_hang arm64_exc_lower_a64_sync
    .org    0x480
    exc_hang arm64_exc_lower_a64_irq
    .org    0x500
    exc_hang arm64_exc_lower_a64_fiq
    .org    0x580
    exc_hang arm64_exc_lower_a64_serror
    .org    0x600
    /* 0x600: lower EL, AArch32, sync */
    exc_hang arm64_exc_lower_a32_sync
    .org    0x680
    exc_hang arm64_exc_lower_a32_irq
    .org    0x700
    exc_hang arm64_exc_lower_a32_fiq
    .org    0x780
    exc_hang arm64_exc_lower_a32_serror

/* ---------------------------------------------------------------
 * Re-usable save/restore helpers (callable).
 * --------------------------------------------------------------- */
    .section .text
    .align  5
    .global arm64_exc_push_all
    .type   arm64_exc_push_all, %function
arm64_exc_push_all:
    push_all
    ret

    .global arm64_exc_pop_all
    .type   arm64_exc_pop_all, %function
arm64_exc_pop_all:
    pop_all
    eret
