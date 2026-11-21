/*
 * los_svc_entry.S — SVC entry (OsExcSvcCall) and exit (OsSyscallEpilogue).
 *
 * Carries the full syscall mechanism surface:
 *  - syscall number from the svc imm8 (stacked frame pc-2), replacing the
 *    IAR r12-handle protocol; dispatch via the C table g_syscallTable
 *  - args via the GCC ABI: frame->r0 is a pointer to the veneer-pushed
 *    {r0-r3}; the kernel dereferences it once
 *  - args 5-8 (stack-passed by the caller above the veneer block) are
 *    re-staged at their AAPCS positions on the kernel stack ("construct
 *    r4-r7 parameters"). The block pointer makes this frame-
 *    type independent - basic, FPU-extended, with or without the
 *    STKALIGN pad - because the veneer pushed the block BEFORE the
 *    exception, so its address never moves.
 *  - FPU (extended-frame) syscalls under LOSCFG_ARCH_FPU_ENABLE:
 *    OsExcSvcCall CLEARS FPCCR.LSPACT when configuring FPCCR for the
 *    bx EXC_RETURN - the pending hardware lazy save is abandoned, and
 *    the graft keeps the ORIGINAL (extended) EXC_RETURN with a 72-byte
 *    FP area reserved above the integer frame. The extended return
 *    preserves CONTROL.FPCA = 1 (mandatory for the PendSV extended-frame
 *    save; a basic graft return clears FPCA and breaks the 204-byte
 *    context geometry - QEMU-proven lockup) and unstacks the unwritten
 *    reserved area into S0-S15 - harmless for softfp
 *    user code (S regs are caller-saved, no FP value lives across a
 *    call). Task switching is NOT lazy: PendSV forces the pending save
 *    to disk and moves S0-S31 + FPSCR with the task context. Kernel
 *    implementations must not use the FPU (softfp kernel rule).
 *  - kernel-sp anchor: [implementation entry SP + 16] - AAPCS caller
 *    territory, above the stack-args area [SP, SP+16), which callees
 *    never write - stores the pre-graft kernel sp. The epilogue reads
 *    it from [PSP+16] instead of storing the post-implementation SP.
 *    The slot also survives blocking - PendSV saves below it - unlike
 *    the TCB copy, which PendSV rewrites.
 *  - bad syscall numbers / null table entries graft a tiny "return
 *    LOS_NOK" implementation instead of jumping straight to the
 *    epilogue: with no graft built, the epilogue's anchor load would
 *    read the USER frame base and later store a user address into
 *    stackPointer.
 *  - retval written back into the user stack args[0]/args[1] slots
 *    (the 32/64-bit return pair), matching the veneer's pop semantics
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

/* bare include like los_dispatch.S: the generated header lives in the
 * target's box/kernel dir, resolved through the propagated include path
 * (arch code must not hardcode a target path) */
#include "los_offsets.h"
/* OS_TASK_STATUS_INUSER: asm-side value home (offsets header exports
 * offsets only - re-exporting a status bit there collided with the C
 * definition, see los_offsets.c). */
#include "los_box_dispatch.h"
#include "syscalls_num.h"

/* Enable the FPU-aware (lazy) syscall path: user tasks run with FPCA = 1,
 * so the hardware pushes extended frames on the user stack and (with
 * FPCCR.LSPEN, set by main.c) leaves the S0-S15/FPSCR save pending. The
 * graft keeps that state pending and reserves the matching FP area, so the
 * syscall is FPU-lazy by construction. The kernel itself stays soft-float
 * (TaskContext unchanged - PendSV uses a runtime CPACR check for d8-d15). */
#define LOSCFG_ARCH_FPU_ENABLE 1

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

    ldr     lr, [r12, #-4]             /* stashed EXC_RETURN */
#ifdef LOSCFG_ARCH_FPU_ENABLE
    /* Config FPCCR for the bx EXC_RETURN below so that returning clears
     * FPCCR.LSPACT. The pending hardware lazy save is
     * ABANDONED: S0-S15/FPSCR stay live in the registers, nothing ever
     * commits to the user-stack FP area. The graft below keeps the
     * ORIGINAL EXC_RETURN (extended stays extended): an extended return
     * preserves CONTROL.FPCA = 1, which the PendSV extended-frame save
     * depends on - a BASIC graft return clears FPCA, downgrades every
     * later exception of this task to basic frames and breaks the
     * 204-byte context geometry (QEMU-proven lockup, d8-d15 loaded from
     * stack fill). */
    ldr     r2, =OS_FPU_FPCCR
    ldr     r3, [r2]
    bic     r3, r3, #OS_FPU_LSPACT
    str     r3, [r2]
#else
    /* Extended (FPU) frames are impossible in this build (CPACR off);
     * guard so a future hard-float build fails visibly, not silently. */
    ldr     r2, =OS_FPU_CPACR
    ldr     r2, [r2]
    and     r2, r2, #OS_FPU_CPACR_ENABLE
    cmp     r2, #OS_FPU_CPACR_ENABLE
    beq     __FpuFrameUnsupported
#endif

    /* r0 = args pointer (frame->r0, GCC ABI). */
    ldr     r0, [r12]

    /* Syscall number: imm8 at [frame->pc - 2]; frame->pc at frame+24. */
    ldr     r3, [r12, #24]
    ldrb    r3, [r3, #-2]
    cmp     r3, #LOSCFG_SYSCALL_NUM
    bhs     __BadSyscall

    /* fn = g_syscallTable[num].fn (entry: {fn, perm}, 8 bytes). */
    ldr     r2, =g_syscallTable
    ldr     r2, [r2, r3, lsl #3]
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

    /* r3 = kernel sp (graft area top). */
    ldr     r3, [r1]

    /*
     * FP gap: reserve 72 bytes in the graft when the
     * incoming frame is FPU-extended (stashed EXC_RETURN bit4 = 0), and
     * return with the ORIGINAL EXC_RETURN. With LSPACT cleared above, the
     * extended graft return unstacks this reserved (unwritten) area into
     * S0-S15; harmless for softfp user code
     * (S regs are caller-saved there, no FP value lives across a call),
     * while task switches still move S0-S31 correctly via the PendSV
     * extended-frame save/restore.
     */
    movs    r12, #0                     /* gap: 0 = basic graft */
#ifdef LOSCFG_ARCH_FPU_ENABLE
    tst     lr, #0x10                   /* bit4: 0 = extended frame */
    it      eq
    moveq   r12, #72
#endif

    /*
     * Graft frame on the kernel stack, deterministic geometry (GCC-ABI
     * offsets):
     *   gap    = 0 (basic) / 72 (FPU-extended incoming frame)
     *   base   = (ksp - 48 - gap) & ~7   8-aligned by construction
     *   [base, base+32)            the hardware frame (bx EXC_RETURN)
     *   [base+32, base+32+gap)     FP area, extended form only - never
     *                              written here; unstacked into S0-S15 on
     *                              the graft return
     *   [base+32+gap, +48+gap)     call args 5-8 at AAPCS [SP+0..16) -
     *                              gcc-verified: a callee reads arg5 at
     *                              [sp+0]; there is no dummy slot
     *   [base+48+gap]              kernel-sp anchor (pre-graft ksp), in
     *                              AAPCS caller territory above the args
     *                              area, which callees never write
     * The implementation is entered at base+32+gap. No STKALIGN bit9 is
     * ever set, so the exception return unstacks exactly 32 or 104 bytes.
     *
     * Register economy: r0 (args pointer), r2 (fn), lr (EXC_RETURN) and
     * r3 (ksp) stay live through the build; r1/r3 are scratch.
     */
    subs    r1, r3, #48
    subs    r1, r1, r12                 /* - gap */
    bic     r1, r1, #7                  /* r1 = graft frame base */
    str     r2, [r1, #24]               /* pc = implementation (fn freed) */
    adds    r2, r1, r12                 /* r2 = base + gap */
    str     r3, [r2, #48]               /* kernel-sp anchor = ksp */
    ldr     r3, =OsSyscallEpilogue
    str     r3, [r1, #20]               /* lr */
    ldr     r3, =0x01000000             /* xPSR: thumb only, no bit9 */
    str     r3, [r1, #28]

    /* Call args 1-4: block[0..3] -> frame r0-r3. */
    ldr     r3, [r0, #0]
    str     r3, [r1, #0]
    ldr     r3, [r0, #4]
    str     r3, [r1, #4]
    ldr     r3, [r0, #8]
    str     r3, [r1, #8]
    ldr     r3, [r0, #12]
    str     r3, [r1, #12]

    /*
     * Call args 5-8: the user CALLER pushed them at [SP_call, SP_call+16)
     * per AAPCS, and SP_call = argsPtr + 16 (the veneer pushed {r0-r3}
     * below them). Copy to [base+32+gap, +48+gap) = implementation SP.
     * r1 is the copy temp; r2 (base + gap) stays live for the entry.
     */
    adds    r0, #16                     /* r0 = &callArg5 */
    adds    r3, r2, #32                 /* r3 = implementation entry SP */
    ldr     r1, [r0, #0]
    str     r1, [r3, #0]
    ldr     r1, [r0, #4]
    str     r1, [r3, #4]
    ldr     r1, [r0, #8]
    str     r1, [r3, #8]
    ldr     r1, [r0, #12]
    str     r1, [r3, #12]

    /* Enter the graft: psp = frame base, "return" into the implementation. */
    subs    r1, r2, r12                 /* r1 = graft frame base */
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
 *
 * Lives in .usr.text (COMMON_TEXT), and that placement is what makes the
 * resume window safe: the unmask + privilege drop + return sequence runs
 * here, so there is no kernel-flash tail after the unmask at all - an
 * interrupt landing anywhere in it saves a COMMON_TEXT pc and resumes
 * legally. No separate trampoline is involved.
 */
    .section .usr.text, "ax"
    .thumb
    .global OsSyscallEpilogue
    .type   OsSyscallEpilogue, %function
    .thumb_func
    .fnstart
    .cantunwind
OsSyscallEpilogue:
    /* r0/r1 = the implementation's return value (AAPCS r0:r1 pair for the
     * one UINT64 API, LOS_CurrNanosec; every other API returns 32 bits in
     * r0 and its r1 is caller-saved scratch). Register budget follows the
     * refs epilogue: r2 = g_runTask chain for the whole body, r3 = work.
     * r1 stays LIVE until the args[1] store below - no parking copy. */
    mrs     r12, primask
    cpsid   i

    ldr     r2, =g_runTask
    ldr     r2, [r2]

    /*
     * Kernel sp bookkeeping: restore the EXACT pre-graft value from the
     * anchor slot at [impl entry SP + 16] - AAPCS caller territory above
     * the args area, so the implementation never overwrote it. The
     * anchor also stays valid across blocking: PendSV saves below the
     * implementation SP, and rewrites the TCB copy instead.
     */
    mrs     r3, psp
    ldr     r3, [r3, #16]              /* anchor = pre-graft kernel sp */
    str     r3, [r2]

    /* Frame base into sp (refs OsSyscallEpilogue shape): this epilogue
     * runs as the grafted PRIVILEGED THREAD, so the thread-mode sp write
     * targets the PSP (SPSEL=1 since the graft entry). From here the
     * frame fields are addressed via [sp, #off] - sp is a dedicated
     * register, so the frame base no longer consumes a GPR and the pc
     * can be read once and kept live to the tail. The kernel-sp anchor
     * above already consumed the graft psp, so psp is free to switch. */
    ldr     sp, [r2, #TASK_CB_USER_SP]  /* user_sp = frame_base - 4 */

    /* retval -> user args[0]/args[1] (the veneer's pop picks them up into
     * r0/r1). args[1] carries the second return word for the one UINT64
     * API (LOS_CurrNanosec, AAPCS r0:r1 pair); for every other API the
     * user's r1 at the call site was caller-saved scratch, so
     * overwriting it is legal. */
    ldr     r3, [sp, #4]                /* frame->r0 = args pointer */
    str     r0, [r3]
    str     r1, [r3, #4]

    /* Restore INUSER, nest = 0, g_sysMode = 1. */
    ldrh    r3, [r2, #TASK_CB_STATUS]
    orr     r3, r3, #OS_TASK_STATUS_INUSER
    strh    r3, [r2, #TASK_CB_STATUS]
    movs    r3, #0
    str     r3, [r2, #TASK_CB_SYSCALL_NEST_CNT]
    ldr     r3, =g_sysMode
    movs    r0, #1
    str     r0, [r3]

    /*
     * Resume at the instruction after svc, refs shape: read every frame
     * field while sp = frame base, pc ONCE into r0 (kept live - only r3
     * works below), then advance sp itself to the resume position.
     * User stack layout:
     *   [sp]      = stashed EXC_RETURN
     *   [sp+4..]  = hw frame (basic 32B / FPU extended 104B, +4 STKALIGN
     *               pad when frame->xpsr bit9 is set)
     *   above the frame = veneer's pushed args block; the resume PSP is
     *               the block start (the veneer's pop {r0-r3} reads
     *               [PSP, PSP+16)).
     * basic: resume = sp + 36; extended: +72 more. The pc/lr/xpsr slots
     * sit in the first 32 bytes of the frame in both cases.
     */
    ldr     r0, [sp, #28]               /* resume pc - read once, live */
    ldr     lr, [sp, #24]               /* restore user LR: the veneer
                                     * returns via bx lr, but the syscall
                                     * machinery destroyed LR (EXC_RETURN,
                                     * graft lr = epilogue, impl BLs).
                                     * Without this the user bx lr jumps
                                     * into the middle of the impl. */
    ldr     r3, [sp, #32]               /* frame->xpsr (STKALIGN bit9) */
#ifdef LOSCFG_ARCH_FPU_ENABLE
    ldr     r1, [sp]                    /* stashed EXC_RETURN */
#endif
    adds    sp, #36                     /* resume PSP, basic frame */
#ifdef LOSCFG_ARCH_FPU_ENABLE
    tst     r1, #0x10                   /* bit4 = 0: extended frame */
    it      eq
    addeq   sp, #72                     /* skip the S0-S15/FPSCR area:
                                         * the hardware RESERVED it at SVC
                                         * entry (FPCA=1) even though
                                         * LSPACT was cleared and nothing
                                         * was ever written there - it
                                         * still occupies user-stack space */
#endif
    tst     r3, #0x200                  /* STKALIGN: hw pushed an align
                                     * word above the frame -> args 4 up */
    it      ne
    addne   sp, #4
    /* msr on M-profile takes r0-r7 only (sp is r13) - park sp in r3,
     * whose STKALIGN value is dead now, then load the PSP. */
    mov     r3, sp
    msr     psp, r3

    /* Drop privilege and resume the user. Safe BECAUSE this function lives
     * in .usr.text (COMMON_TEXT): an interrupt landing anywhere in the
     * unmask-to-branch span saves a COMMON_TEXT pc, which resumes legally
     * under the user MPU view - there is no kernel-flash tail and no
     * trampoline. PRIMASK restores BEFORE the privilege drop: after the
     * drop, msr primask is ignored in unprivileged mode. CONTROL is
     * read-modify-written (mrs control / orr #1 keeps every bit) - it is
     * the only carrier of this task's FPCA across the manual (non-exception)
     * resume, and a cleared FPCA would stop the hardware from stacking
     * this task's FP state on the next exception. The hardware-stacked
     * frame PC carries no Thumb bit (T lives in xPSR bit24) - the orr
     * below forces it, else the manual bx takes INVSTATE.
     * Callee-saved r4-r11 ride through untouched (live user values);
     * r0-r3 are caller-saved and the veneer's pop {r0-r3} restores
     * them after the return. */
    orr     r0, r0, #1
    mrs     r3, control
    orr     r3, r3, #1                /* CONTROL: PSP + unprivileged */
    msr     primask, r12              /* unmask - inside COMMON_TEXT */
    msr     control, r3               /* drop privilege */
    bx      r0
    .fnend
