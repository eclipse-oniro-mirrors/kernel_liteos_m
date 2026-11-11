/*
 * los_svc_entry.S — SVC entry (OsExcSvcCall) and exit (OsSyscallEpilogue).
 *
 * Phase-1 adaptation of refs los_hw_syscalls.S (182 lines), now carrying
 * the full mechanism surface of the reference:
 *  - syscall number from the svc imm8 (stacked frame pc-2), replacing the
 *    IAR r12-handle protocol; dispatch via the C table g_syscallTable
 *  - args via the GCC ABI: frame->r0 is a pointer to the veneer-pushed
 *    {r0-r3}; the kernel dereferences it once (phase-1 known cost, M4R)
 *  - args 5-8 (stack-passed by the caller above the veneer block) are
 *    re-staged at their AAPCS positions on the kernel stack ("construct
 *    r4-r7 parameters", refs entry). The block pointer makes this frame-
 *    type independent - basic, FPU-extended, with or without the
 *    STKALIGN pad - because the veneer pushed the block BEFORE the
 *    exception, so its address never moves.
 *  - FPU (extended-frame) syscalls under LOSCFG_ARCH_FPU_ENABLE: the
 *    user's S0-S15/FPSCR stay LIVE through the syscall - FPCCR.LSPACT
 *    is cleared so the pending lazy save cannot fire into the graft
 *    area, and the graft returns through a forced BASIC frame so no S
 *    state is ever popped from uninitialized kernel stack memory (the
 *    refs path reserved 72 bytes and let the deferred save fire there;
 *    a basic return cannot read S registers at all). Blocking syscalls
 *    still preserve the FPU state: PendSV saves/restores it in its own
 *    extended frame (FPCA = 1). Kernel implementations must not use the
 *    FPU (softfp kernel rule).
 *  - kernel-sp anchor: [implementation entry SP + 16] - AAPCS caller
 *    territory, above the stack-args area [SP, SP+16), which callees
 *    never write - stores the pre-graft kernel sp. The epilogue reads
 *    it from [PSP+16] instead of storing the post-implementation SP
 *    (the refs epilogue's "add sp, #16" was the analog arithmetic).
 *    The slot also survives blocking - PendSV saves below it - unlike
 *    the TCB copy, which PendSV rewrites.
 *  - bad syscall numbers / null table entries graft a tiny "return
 *    LOS_NOK" implementation instead of jumping straight to the
 *    epilogue: with no graft built, the epilogue's anchor load would
 *    read the USER frame base and later store a user address into
 *    stackPointer.
 *  - retval written back into the user stack args[0] slot, matching the
 *    veneer's pop semantics
 *  - frame graft: a hand-built frame on the task kernel stack is entered
 *    via bx EXC_RETURN, so the implementation runs as a privileged thread
 *    (real blocking, clean exception state)
 *
 * Register discipline: the entry uses ONLY r0-r3 and r12 (all hardware-
 * stacked in the user frame) until the graft switches context; r4-r11
 * still hold live user values until then. Offsets come from the generated
 * los_offsets.h; layout drift breaks the build via los_offsets.c.
 */
.syntax unified
.cpu cortex-m4
.fpu softvfp
.thumb

#include "../kernel/los_offsets.h"
#include "syscalls_num.h"

.equ    OS_FPU_CPACR,        0xE000ED88
.equ    OS_FPU_CPACR_ENABLE, 0x00F00000
.equ    OS_FPU_FPCCR,        0xE000EF34
.equ    OS_FPU_LSPACT,       0x00000001
.equ    EXC_RETURN_BASIC,    0xFFFFFFFD

    .section .kernel.text, "ax"
    .thumb

    .global OsExcSvcCall
    .global OsSyscallEpilogue
    .type  OsExcSvcCall, %function
    .fnstart
    .cantunwind
OsExcSvcCall:
    /* Stash EXC_RETURN below the user frame; r12 = frame base. */
    mrs     r12, psp
    subs    r0, r12, #4
    str     lr, [r0]

    /* TCB bookkeeping: user sp, clear INUSER, nest = 1. */
    ldr     r1, =g_runTask
    ldr     r1, [r1]
    str     r0, [r1, #TASK_CB_USER_SP]
    ldrh    r2, [r1, #TASK_CB_STATUS]
    bic     r2, r2, #OS_TASK_STATUS_INUSER
    strh    r2, [r1, #TASK_CB_STATUS]
    movs    r2, #1
    str     r2, [r1, #TASK_CB_SYSCALL_NEST_CNT]

    /* Privilege up, mark kernel mode. */
    mrs     r2, control
    bic     r2, r2, #1
    msr     control, r2
    ldr     r3, =g_sysMode
    movs    r2, #0
    str     r2, [r3]

#ifdef LOSCFG_ARCH_FPU_ENABLE
    /*
     * FPU path (refs los_hw_syscalls.S, reworked for the GCC ABI):
     * extended frame = stashed EXC_RETURN bit4 == 0. Keep the user's
     * FPU state LIVE through the syscall:
     *   - clear FPCCR.LSPACT: the deferred lazy save must not fire into
     *     the graft area on the bx below;
     *   - return through a forced BASIC frame (EXC_RETURN_BASIC): the
     *     pop restores r0-r3/r12/lr/pc/xpsr only, so S0-S15/FPSCR ride
     *     along untouched (the refs reserved 72 bytes and let the
     *     deferred save fire there - restoring whatever it wrote - but
     *     a basic return cannot read S registers from uninitialized
     *     kernel memory at all).
     * The epilogue still reads the ORIGINAL stash to skip the user
     * frame's S area when resuming. Blocking syscalls are covered by
     * PendSV's own extended-frame save/restore (FPCA = 1).
     */
    ldr     r2, [r12, #-4]            /* stashed EXC_RETURN */
    tst     r2, #0x10                  /* bit4: 0 = extended frame */
    bne     1f
    ldr     r0, =OS_FPU_FPCCR
    ldr     r3, [r0]
    bic     r3, r3, #OS_FPU_LSPACT     /* cancel pending lazy save */
    str     r3, [r0]
    ldr     lr, =EXC_RETURN_BASIC      /* forced basic-frame graft return */
    b       2f
1:
    ldr     lr, [r12, #-4]             /* basic frame: stashed value */
2:
#else
    /* Extended (FPU) frames are impossible in this build (CPACR off);
     * guard so a future hard-float build fails visibly, not silently. */
    ldr     r2, =OS_FPU_CPACR
    ldr     r2, [r2]
    and     r2, r2, #OS_FPU_CPACR_ENABLE
    cmp     r2, #OS_FPU_CPACR_ENABLE
    beq     __FpuFrameUnsupported
    ldr     lr, [r12, #-4]
#endif

    /* r3 = kernel sp (graft area top). */
    ldr     r3, [r1]

    /* Syscall number: imm8 at [frame->pc - 2]; frame->pc at frame+24. */
    ldr     r0, [r12, #24]
    ldrb    r0, [r0, #-2]
    cmp     r0, #LOSCFG_SYSCALL_NUM
    bhs     __BadSyscall

    /* fn = g_syscallTable[num].fn (entry: {fn, perm}, 8 bytes). */
    ldr     r2, =g_syscallTable
    ldr     r2, [r2, r0, lsl #3]
    cmp     r2, #0
    beq     __BadSyscall
    b       3f

__BadSyscall:
    /* Graft the LOS_NOK stub instead of jumping straight to the
     * epilogue: the epilogue's kernel-sp bookkeeping reads the anchor
     * from [impl entry SP]; with no graft built, psp would still be
     * the USER frame base and the anchor load would both read garbage
     * and store a user address into stackPointer. */
    ldr     r2, =__BadSyscallImpl
3:

    /* r0 = args pointer (frame->r0, GCC ABI). */
    ldr     r0, [r12]

    /*
     * Graft frame on the kernel stack, deterministic geometry (refs
     * los_hw_syscalls.S shape, GCC-ABI offsets):
     *   base   = (ksp - 48) & ~7   8-aligned by construction
     *   [base, base+32)            the hardware frame (bx EXC_RETURN)
     *   [base+32, base+48)         call args 5-8 at AAPCS [SP+0..16) -
     *                              gcc-verified: a callee reads arg5 at
     *                              [sp+0]; there is no dummy slot
     *   [base+48]                  kernel-sp anchor (pre-graft ksp), in
     *                              AAPCS caller territory above the args
     *                              area, which callees never write
     * The implementation always enters at base+32: 8-aligned, SP exactly
     * base+32. No STKALIGN bit9 is ever set, so the exception return
     * unstacks exactly 32 bytes - no +4 adjustment, nothing dependent on
     * the hardware stack-alignment-on-return behavior. A 4-mod-8 ksp
     * (the common case: TaskContext is 68 bytes on an 8-aligned stack
     * top) merely shifts the whole block 4 bytes lower; the anchor keeps
     * the bookkeeping exact where the refs epilogue used add sp, #16.
     *
     * Register economy for the wide ldm/stm pairs below: four values must
     * stay live through the build - r0 (args pointer), r1 (frame base),
     * EXC_RETURN (for the final bx) and fn - so the pairs only become
     * possible after (a) fn is stored to the pc slot early, (b)
     * EXC_RETURN is PARKED in the frame's r12 slot (AAPCS incoming
     * scratch), and (c) the anchor is stored while r3 still holds ksp.
     */
    subs    r1, r3, #48
    bic     r1, r1, #7                  /* r1 = graft frame base */
    str     r3, [r1, #48]               /* kernel-sp anchor = ksp */
    str     lr, [r1, #16]               /* park EXC_RETURN (frame r12 slot) */
    str     r2, [r1, #24]               /* pc = implementation */

    ldmia   r0, {r2, r3, r12, lr}       /* block[0..3] = call args 1-4 -> frame r0-r3 */
    stmia   r1, {r2, r3, r12, lr}

    ldr     r3, =OsSyscallEpilogue
    str     r3, [r1, #20]               /* lr */
    ldr     r3, =0x01000000             /* xPSR: thumb only, no bit9 */
    str     r3, [r1, #28]

    /*
     * Call args 5-8: the user CALLER pushed them at [SP_call, SP_call+16)
     * per AAPCS, and SP_call = argsPtr + 16 (the veneer pushed {r0-r3}
     * below them). Copy to [base+32, base+48). r0 and r3 are both dead
     * after this block, so the copy runs as writeback ldmia/stmia pairs.
     */
    adds    r0, #16                     /* r0 = &callArg5 */
    adds    r3, r1, #32                 /* r3 = &callArg5 destination */
    ldmia   r0!, {r2, r12, lr}          /* call args 5-7 */
    stmia   r3!, {r2, r12, lr}
    ldr     r2, [r0]                    /* call arg 8 */
    str     r2, [r3]

    /* Enter the graft: psp = frame base, "return" into the implementation. */
    ldr     lr, [r1, #16]               /* unpark EXC_RETURN */
    msr     psp, r1
    bx      lr
    .fnend

/*
 * Bad-syscall stub implementation: runs through the normal graft so the
 * epilogue's bookkeeping stays exact. mvn r0 = 0xFFFFFFFF (LOS_NOK).
 */
    .type  __BadSyscallImpl, %function
    .fnstart
    .cantunwind
__BadSyscallImpl:
    mvn     r0, #0
    bx      lr
    .fnend

__FpuFrameUnsupported:
    /* Soft-float build only; a hard-float build must compile the FPU
     * branch above instead of reaching this guard. */
    cpsid   i
    b       __FpuFrameUnsupported

/*
 * OsSyscallEpilogue — implementation returned (r0 = retval), thread mode,
 * privileged, psp = kernel stack (implementation entry SP). Restore the
 * user context manually.
 */
    .type  OsSyscallEpilogue, %function
    .fnstart
    .cantunwind
OsSyscallEpilogue:
    mrs     r12, primask
    cpsid   i

    ldr     r1, =g_runTask
    ldr     r1, [r1]

    /*
     * Kernel sp bookkeeping: restore the EXACT pre-graft value from the
     * anchor slot at [impl entry SP + 16] - AAPCS caller territory above
     * the args area, so the implementation never overwrote it. The
     * anchor also stays valid across blocking: PendSV saves below the
     * implementation SP, and rewrites the TCB copy instead.
     */
    mrs     r2, psp
    ldr     r2, [r2, #16]              /* anchor = pre-graft kernel sp */
    str     r2, [r1]

    /* retval -> user args[0] (veneer's pop picks it up into r0). */
    ldr     r2, [r1, #TASK_CB_USER_SP]  /* user_sp = frame_base - 4 */
    ldr     r2, [r2, #4]                /* frame->r0 = args pointer */
    str     r0, [r2]

    /* Restore INUSER, nest = 0, g_sysMode = 1. */
    ldrh    r3, [r1, #TASK_CB_STATUS]
    orr     r3, r3, #OS_TASK_STATUS_INUSER
    strh    r3, [r1, #TASK_CB_STATUS]
    movs    r3, #0
    str     r3, [r1, #TASK_CB_SYSCALL_NEST_CNT]
    ldr     r3, =g_sysMode
    movs    r0, #1
    str     r0, [r3]

    /*
     * Resume at the instruction after svc. User stack layout:
     *   [user_sp]       = stashed EXC_RETURN
     *   [user_sp+4..]   = hw frame (basic 32B / FPU extended 104B, +4
     *                     STKALIGN pad when frame->xpsr bit9 is set)
     *   above the frame = veneer's pushed args block; the resume PSP is
     *                     the block start (the veneer's pop {r0-r3}
     *                     reads [PSP, PSP+16)).
     * basic: PSP = user_sp + 36; extended: +72 more. The pc/lr/xpsr
     * slots sit in the first 32 bytes of the frame in both cases.
     */
    ldr     r2, [r1, #TASK_CB_USER_SP]
    ldr     r3, [r2, #28]               /* resume pc */
    ldr     lr, [r2, #24]               /* restore user LR: the veneer
                                     * returns via bx lr, but the syscall
                                     * machinery destroyed LR (EXC_RETURN,
                                     * graft lr = epilogue, impl BLs).
                                     * Without this the user bx lr jumps
                                     * into the middle of the impl. */
    adds    r2, #36                     /* resume PSP, basic frame */
#ifdef LOSCFG_ARCH_FPU_ENABLE
    ldr     r0, [r2, #-36]              /* stashed EXC_RETURN */
    tst     r0, #0x10                   /* bit4 = 0: extended frame */
    it      eq
    addeq   r2, #72                     /* skip the S0-S15/FPSCR area */
#endif
    ldr     r0, [r1, #TASK_CB_USER_SP]  /* frame->xpsr at fixed +32 */
    ldr     r0, [r0, #32]
    tst     r0, #0x200                  /* STKALIGN: hw pushed an align
                                     * word above the frame -> args 4 up */
    it      ne
    addne   r2, #4
    msr     psp, r2

    /* Drop privilege and resume through the COMMON_TEXT trampoline
     * (OsBoxUsrEntry, los_dispatch.S). Doing the unmask here - in
     * kernel flash, privileged - opens a 2-instruction window where an
     * interrupt (with reschedule) saves a kernel-flash pc with INUSER
     * set; the resume then refetches it unprivileged and the MPU
     * denies kernel flash (hardware-confirmed crash class: two crashes
     * at this exact tail, ~1h uptime each; design doc pitfall 25).
     * The trampoline keeps the unmasked instant inside COMMON_TEXT and
     * a window reschedule recovers fully. Protocol: r12 = PRIMASK
     * (held since the epilogue's first mrs), r2 = CONTROL, r3 = user
     * pc. The hardware-stacked frame PC carries no Thumb bit (T lives
     * in xPSR bit24) - the orr below forces it, else the manual bx
     * takes INVSTATE. Callee-saved r4-r11 ride through untouched (live
     * user values); r0-r3 are caller-saved and the veneer's
     * pop {r0-r3} restores them after the return. */
    orr     r3, r3, #1
    movs    r2, #3                    /* CONTROL: PSP + unprivileged */
    b       OsBoxUsrEntry             /* masked jump (r12 = saved PRIMASK):
                                       * the unmask lands in COMMON_TEXT */
    .fnend
