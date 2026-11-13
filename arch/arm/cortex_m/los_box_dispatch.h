/*
 * los_box_dispatch.h — BOX (user-mode isolation) PendSV hooks.
 *
 * BOX-only consumers, all compiled solely under LOSCFG_BOX:
 * los_dispatch_box.S (the BOX dispatch variant), los_svc_entry.S and
 * los_box_uspace.S — hence no !LOSCFG_BOX fallback shape here. The
 * vanilla los_dispatch.S never includes this header.
 *
 * Register contracts (caller = the V7M PendSV path in los_dispatch.S):
 *   BOX_PENDSV_SAVE     in: r0 = saved context base, r6 = g_oldTask
 *                       out: r0 = value for oldTask->stackPointer
 *                       clobbers r2
 *   BOX_PENDSV_RESTORE  in: r0 = newTask, r1 = newTask->stackPointer,
 *                           r6 = g_oldTask, lr = EXC_RETURN
 *                       out: r1 = sp to restore (user sp when INUSER),
 *                            CONTROL mode bits + g_sysMode updated,
 *                            MPU switched to the new box (C call, r0/lr
 *                            preserved across it)
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
 * M5A additionally maintains the refs current-context globals (refs
 * dispatch.S / OsSwitchBoxContext semantics):
 *   - g_curBoxId: the running task's box id, EVERY switch (kernel tasks
 *     carry OS_KERNEL_BOX on curBoxId);
 *   - g_boxLocalPool[0]: the new box's heap address, only when switching
 *     INTO a box task (the user world reads it from usr rodata directly —
 *     its allocator calls take the pool explicitly, no syscall involved).
 *
 * Register budget: r4/r5 are dead at the call site (the context pop below
 * reloads r4-r12) and r4 additionally survives the BoxMpuSwitch BL
 * (callee-saved per AAPCS).
 */
.macro BOX_PENDSV_RESTORE
    ldrb    r4, [r0, #TASK_CB_CUR_BOX_ID]
    ldr     r5, =g_curBoxId
    str     r4, [r5]
    ldr     r2, [r0, #TASK_CB_BOX_CB]
    ldr     r3, [r6, #TASK_CB_BOX_CB]
    cmp     r2, r3
    beq     9\@f
    cbz     r2, 8\@f
    ldr     r4, [r2, #BOX_CB_LAYOUT0_MEMPOOL]
    push    {r0, lr}
    mov     r0, r2
    bl      BoxMpuSwitch
    pop     {r0, lr}
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


