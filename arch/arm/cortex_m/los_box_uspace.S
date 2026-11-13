/*
 * los_box_uspace.S — refs first-dispatch descent (uspace_entry.S semantics,
 * adapted to the M4H 204B context geometry).
 *
 * *** PARKED (pitfall 41, M5B): compiled but unreferenced. The descent
 * crashed on the first BLOCKING syscall (QEMU: UNDEFINSTR at a garbage PC,
 * CFSR=0x40000; the user task entered with r4-r11 loaded from the kernel
 * stack filler) — interaction with the phase-1 graft/epilogue machinery not
 * root-caused. Box tasks enter user mode the born-INUSER way instead
 * (OsBoxTskUserStackInit + INUSER at create). This stub stays for the
 * dedicated descent-debugging milestone. ***
 *
 * OsUserTaskEntry (kernel-mode prelude) calls OsUserspaceEnter as the very
 * last action; the stub drops the task into its crafted user context:
 *   - mark the running task INUSER (subsequent PendSV switches take the
 *     user-context path),
 *   - park MSP just above the (now consumed) crafted kernel context so
 *     later syscall/exception stacking starts from a clean base (refs
 *     achieves the same by unwinding the prelude frames with add sp),
 *   - publish g_sysMode=1 for the veneers, switch CONTROL to PSP +
 *     unprivileged, then enter the user hardware frame directly (no
 *     exception return: r0..r3/r12/lr from the frame, PC via bx with its
 *     bit0 thumb marker).
 *
 * AAPCS note: this stub clobbers r4-r8 and never returns - legal only
 * because the caller (OsUserTaskEntry) abandons its frame on purpose.
 *
 * Placement: .usr.text (COMMON_TEXT) - entered privileged, executes its
 * tail unprivileged, so it must live in the shared user-executable area.
 */
#ifdef LOSCFG_BOX

#include "los_offsets.h"
/* OS_TASK_STATUS_INUSER: asm-side value home (offsets header exports
 * offsets only, see los_offsets.c) */
#include "los_box_dispatch.h"

    .syntax unified
    .arch    armv7e-m
    .thumb

    .section .usr.text.OsUserspaceEnter, "ax"
    .global  OsUserspaceEnter
    .type    OsUserspaceEnter, %function
    .thumb_func
OsUserspaceEnter:
    /* No prologue on purpose (see header): never returns to the caller. */
    cpsid    i

    /* running task -> INUSER */
    ldr      r4, =g_runTask
    ldr      r4, [r4]
    ldrh     r5, [r4, #TASK_CB_STATUS]
    ldr      r6, =OS_TASK_STATUS_INUSER
    orrs     r5, r5, r6
    strh     r5, [r4, #TASK_CB_STATUS]

    /* PSP = user stack pointer, MSP = kernel ctx top + 204 (clean base) */
    ldr      r5, [r4, #TASK_CB_USER_SP]
    ldr      r6, [r4]                  /* stackPointer @ offset 0 */
    adds     r6, r6, #204
    msr      msp, r6
    msr      psp, r5

    /* user mode flag for the veneers (usr rodata) */
    ldr      r7, =g_sysMode
    movs     r8, #1
    str      r8, [r7]

    cpsie    i                         /* last privileged op */
    movs     r8, #3                    /* PSP + unprivileged */
    msr      control, r8
    isb

    /* enter the crafted user hardware frame at userStackPointer+100 */
    adds     r5, r5, #100
    ldmfd    r5!, {r0-r3, r12, lr}
    ldr      r8, [r5]                  /* pc, bit0 = thumb marker */
    bx       r8

    .size    OsUserspaceEnter, . - OsUserspaceEnter

#endif /* LOSCFG_BOX */
