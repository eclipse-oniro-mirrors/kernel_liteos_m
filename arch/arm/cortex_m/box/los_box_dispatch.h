/*
 * los_box_dispatch.h — BOX (user-mode isolation) PendSV hooks.
 *
 * BOX-only consumers, all compiled solely under LOSCFG_BOX:
 * los_dispatch_box.S (the BOX dispatch variant), los_svc_entry.S and
 * los_box_uspace.S — hence no !LOSCFG_BOX fallback shape here. The
 * vanilla los_dispatch.S never includes this header.
 *
 * Register contracts (caller = the V7M PendSV path in los_dispatch_box.S):
 *   BOX_PENDSV_SAVE     in: r0 = saved context base, r6 = g_oldTask
 *                       out: r0 = value for oldTask->stackPointer
 *                       clobbers r2
 *   BOX_PENDSV_RESTORE  in: r0 = newTask, r1 = newTask->stackPointer,
 *                           r6 = g_oldTask, r12 = PRIMASK, lr = EXC_RETURN
 *                       out: r1 = sp to restore (user sp when INUSER),
 *                            CONTROL mode bits + g_sysMode updated,
 *                            MPU switched to the new box. BoxMpuSwitch is
 *                            AAPCS C and may scratch r0-r3/r12/lr, so every
 *                            one of those that is live here (r0 newTask,
 *                            r1 stackPointer, r12 PRIMASK, lr EXC_RETURN)
 *                            is pushed around the BL.
 *                       clobbers r2, r3
 *
 * Offsets (TASK_CB_*) come from the generated los_offsets.h (the target
 * box/offsets action; resolved via the board-provided include path — no
 * hardcoded target path).
 *
 * BUILD NOTE: the ninja graph has no depfile for .S #includes — editing
 * this header does NOT trigger a rebuild of its consumers. Touch
 * los_dispatch_box.S / los_svc_entry.S / los_box_uspace.S after changing
 * it (same family as los_offsets.h).
 */
#include "los_offsets.h"

/* BOX and KERNEL_SIGNAL are mutually exclusive (enforced in
 * kernel/extended/signal/Kconfig; this #error is the backstop for a config
 * that bypasses Kconfig).
 *
 * Reason, concretely: the signal-context restore path this dispatch family
 * used to carry (SIGNAL_CONTEXT_RESTORE in los_dispatch_box.S) branched
 * straight to SignalContextRestore when OsSignalTaskContextRestore returned a
 * signal stack, i.e. PAST BOX_PENDSV_RESTORE below. Everything that macro
 * maintains then kept describing the task being switched OUT:
 *   g_curBoxId      -> OsGetCurBoxCB() resolves the WRONG box for every
 *                      pool/SID/permission decision the task makes;
 *   MPU R3/R4       -> the task's own text/data are unmapped while ANOTHER
 *                      box's are mapped and accessible;
 *   g_boxLocalPool  -> the usr-world allocator draws from the wrong heap;
 *   CONTROL/g_sysMode -> privilege and the veneers' kernel/user view desync.
 * None of that faults, so it surfaced as an isolation break, not a crash.
 * That path has since been deleted from los_dispatch_box.S outright. Porting
 * the vanilla SIGNAL_CONTEXT_RESTORE back would require rewriting it to
 * re-establish box state first (the signal target's boxCB, not the outgoing
 * task's) before resuming on the signal stack. */
#if defined(LOSCFG_KERNEL_SIGNAL) && (LOSCFG_KERNEL_SIGNAL == 1)
#error "LOSCFG_KERNEL_SIGNAL cannot be enabled together with LOSCFG_BOX: the signal-context restore path bypasses BOX_PENDSV_RESTORE and would resume the task under the previous task's box state (MPU/g_curBoxId/g_boxLocalPool/g_sysMode). See kernel/extended/signal/Kconfig."
#endif
/* asm-side status-bit value; mirrors los_task_base.h's C definition
 * (0x0200), which los_offsets.c _Static_asserts against drift. The
 * asm-offsets header exports offsets only - re-exporting a status bit
 * there collided with the C definition in TUs including both (backtrace
 * -Werror redefinition). This is the single asm-side home shared by the
 * dispatch, svc-entry and uspace asm. */
#define OS_TASK_STATUS_INUSER 0x0200

/*
 * A task switched out in user mode keeps its context on the USER stack;
 * record it there — stackPointer stays at the kernel sp (dual stack).
 */
.macro BOX_PENDSV_SAVE
    ldrh    r2, [r6, #TASK_CB_STATUS]
    tst     r2, #OS_TASK_STATUS_INUSER
    beq     9\@f
    str     r0, [r6, #TASK_CB_USER_SP]
    ldr     r0, [r6]
9\@:
.endm

/*
 * Switch the MPU to the new box (only on box change), then the mode +
 * dual-stack select: pick the user stack and drop privilege for a task
 * suspended in user mode (g_sysMode tracks it for the veneers).
 *
 * The new-vs-old boxCB compare below is a cheap FAST PATH: it avoids the call
 * entirely for same-box task switches. BoxMpuSwitch has a second guard against
 * the box its R3/R4 are actually programmed for, which covers what this
 * compare cannot see - a box -> kernel -> same-box round trip (the cbz skips
 * the kernel case, so the MPU still holds the previous box and reprogramming
 * it on the way back would be pure waste).
 *
 * The restore path additionally maintains the current-context globals:
 *   - g_curBoxId: the running task's box id, EVERY switch (kernel tasks
 *     carry OS_KERNEL_BOX on curBoxId);
 *   - g_boxLocalPool[0]: the new box's heap address, only when switching
 *     INTO a box task (the user world reads it from usr rodata directly —
 *     its allocator calls take the pool explicitly, no syscall involved).
 *
 * Register budget across the BoxMpuSwitch BL (pit 58): BoxMpuSwitch is plain
 * AAPCS C - it may freely scratch r0-r3, r12 and lr. FOUR of those are live
 * here and used to be held only by luck:
 *   r0  = newTask (consumed by everything below the call);
 *   r1  = newTask->stackPointer, loaded by the PendSV caller BEFORE this
 *         macro and consumed by the handler tail's context pop (ldmfd r1!).
 *         It is NOT reloaded for a task restored in KERNEL mode - only the
 *         INUSER arm below reloads it from TASK_CB_USER_SP - so a call that
 *         scratches r1 makes the tail pop the context from garbage. The
 *         M6N-era BoxMpuSwitch codegen happened to use r2/r3 only; a cosmetic
 *         refactor moved the scratch to r1 and crashed the first different-box
 *         restore of a syscall-parked task (guard-hit early returns and the
 *         INUSER reload had masked the clobber until a second box joined the
 *         schedule).
 *   r12 = PRIMASK, saved by the PendSV prologue, restored at the tail
 *         (msr primask, r12);
 *   lr  = EXC_RETURN.
 * All four are pushed around the BL. r4 (the box mempool for g_boxLocalPool)
 * needs no push: AAPCS keeps it alive as callee-saved. r5 is dead across the
 * call (reloaded right after it).
 */
.macro BOX_PENDSV_RESTORE
    ldrb    r4, [r0, #TASK_CB_CUR_BOX_ID]
    ldr     r5, =g_curBoxId
    str     r4, [r5]
    ldr     r2, [r0, #TASK_CB_BOX_CB]
    /* r3: previous boxCB, or NULL when discarding a faulted BOX. */
    cmp     r2, r3
    beq     9\@f
    cbz     r2, 8\@f
    ldr     r4, [r2, #BOX_CB_LAYOUT0_MEMPOOL]
    push    {r0, r1, r12, lr}
    mov     r0, r2
    bl      BoxMpuSwitch
    pop     {r0, r1, r12, lr}
    ldr     r5, =g_boxLocalPool
    str     r4, [r5]
8\@:
9\@:
    ldrh    r2, [r0, #TASK_CB_STATUS]
    mrs     r3, control
    bic     r3, r3, #1
    tst     r2, #OS_TASK_STATUS_INUSER
    itt     ne
    ldrne   r1, [r0, #TASK_CB_USER_SP]
    orrne   r3, r3, #1
    msr     control, r3
    ldr     r2, =g_sysMode
    and     r3, r3, #1
    str     r3, [r2]
.endm


