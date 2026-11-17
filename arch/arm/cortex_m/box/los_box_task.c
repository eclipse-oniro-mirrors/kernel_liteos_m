/*
 * los_box_task.c — BOX task context crafting. The crafted context is
 * entered the born-INUSER way - the task is created "as if it had been
 * switched out in user mode", so the very first PendSV dispatch lands in
 * the entry function unprivileged with psp on the user stack.
 *
 * PARKED: the alternate first-dispatch flavor (OsUserTaskEntry
 * kernel-mode prelude + OsUserspaceEnter descent, compiled in
 * los_box_uspace.S) crashed on the first BLOCKING syscall with
 * UNDEFINSTR-at-garbage-PC (QEMU, CFSR=0x40000) - interaction with the
 * graft/epilogue machinery not root-caused. The born-INUSER entry is
 * the QEMU+board-validated equivalent and stays the mechanism.
 *
 * Context geometry (unified 204B, from the user stack pointer):
 *   [sp+0  ] 64B gap (d8-d15 area - zeroed: fresh-task FP state defined)
 *   [sp+64 ] TaskContext: r4-r12 + priMask (36B, zeroed)
 *   [sp+100] HW frame r0..xPSR (32B)
 *   [sp+132] FP area S0-S15+FPSCR+pad (72B, popped by an extended return)
 * The first dispatch restores r4-r12 from the zeroed gap (dead registers,
 * primask slot reads 0) and pops the extended frame - S regs defined.
 * The 64/72 numbers are not literals in the code below: they are derived
 * from EXC_CONTEXT_S and pinned by _Static_assert (see BOX_USRCTX_FP_GAP /
 * BOX_USRCTX_FP_TAIL), because los_dispatch_box.S hardcodes the same layout
 * in asm and cannot follow a struct change on its own.
 */
#include "los_typedef.h"
#include "los_task_pri.h"
#include "los_arch_context.h"
#include "los_arch_interrupt.h"   /* EXC_CONTEXT_S: the frame geometry source */
#include "los_offsets.h"          /* generated: what the dispatch asm consumes */
#include "securec.h"
#include "los_box_pri.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* User task exit trampoline (COMMON_FLASH): a box entry function that
 * simply returns lands here and terminates via LOS_TaskUsrExit. */
extern VOID OsUsrTaskExit(VOID);

/* Frame geometry, derived instead of literal.
 *
 * TaskContext is compiled SHORT in this TU: the arch module carries no
 * -mfpu, so __SOFTFP__ is defined, CMSIS derives __FPU_USED == 0, and
 * los_arch_context.h #if's out TaskContext's own S0-S15/FPSCR tail. What is
 * left, sizeof(TaskContext) == 68, is exactly the part the crafting below
 * wants to write field-by-field: r4-r12 + priMask (36) followed by the
 * r0..xPSR hardware frame (32).
 *
 * The two FP areas the first dispatch still needs therefore have to be
 * added by hand around it. EXC_CONTEXT_S is the one place both sizes are
 * already structural - the FP condition in los_arch_interrupt.h has a
 * LOSCFG_BOX arm, and this file is BOX-only (arch BUILD.gn), so S0/S16 always
 * exist here:
 *   FP_GAP  = the S16-S31 (d8-d15) slot placed below the TaskContext
 *   FP_TAIL = S0-S15 + FPSCR + reserved above it, popped by an extended return
 *
 * Deriving them ties this file to the struct the exception entry actually
 * uses, so a geometry change breaks the build rather than quietly misplacing
 * the crafted context by 64 or 72 bytes.
 *
 * The SAME two values are emitted into the generated los_offsets.h as
 * EXC_CTX_FP_GAP / EXC_CTX_FP_TAIL, and that is what los_dispatch_box.S now
 * uses instead of literal 100/72/36. So there are three consumers of one
 * geometry: this file derives it from the struct, the generator derives it
 * from the struct, and the asm reads the generator's output. The asserts
 * below tie the first two together, which closes the loop on the third. */
#define BOX_USRCTX_FP_GAP   LOS_OFF_SET_OF(EXC_CONTEXT_S, uwR4)
#define BOX_USRCTX_FP_TAIL  (sizeof(EXC_CONTEXT_S) - LOS_OFF_SET_OF(EXC_CONTEXT_S, S0))

/* los_dispatch_box.S hardcodes the same geometry in asm ("r4-r12 at
 * ctx[0..36), d8-d15 at ctx+36, PSP = ctx+100", plus "add r12, r12, #72"),
 * and asm cannot follow a struct change. These asserts are the only thing
 * that couples the two: if TaskContext ever turns wide (arch module gains
 * -mfpu, or the BOX arm is mirrored into los_arch_context.h) the FP tail
 * would be reserved twice and the first dispatch would land 72 bytes off;
 * if EXC_CONTEXT_S changes shape, FP_GAP/FP_TAIL no longer describe what the
 * hardware pushes. Either way the asm has to be updated in the same change. */
_Static_assert(sizeof(TaskContext) == 68,
               "the crafting below reserves both FP areas by hand around a SHORT "
               "TaskContext; a wide one would double-reserve the tail");
_Static_assert((BOX_USRCTX_FP_GAP == 64) && (BOX_USRCTX_FP_TAIL == 72),
               "frame geometry drifted - the crafted context below and the "
               "first-dispatch asm both assume the 204-byte unified layout");
/* Cross-check against the generated header the asm consumes: catches the case
 * where EXC_CONTEXT_S differs between this TU and the offsets generator (they
 * use different flag sets - notably no -mfpu there). */
_Static_assert(BOX_USRCTX_FP_GAP == EXC_CTX_FP_GAP,
               "los_offsets.h disagrees with EXC_CONTEXT_S in this TU");
_Static_assert(BOX_USRCTX_FP_TAIL == EXC_CTX_FP_TAIL,
               "los_offsets.h disagrees with EXC_CONTEXT_S in this TU");

/*
 * Craft the initial user context on the box stack: PC = the real entry,
 * LR = the exit trampoline, INUSER set by the caller. Returns the user
 * stack pointer.
 */
VOID *OsBoxTskUserStackInit(LosTaskCB *taskCB, VOID *userStackBase, UINT32 userStackSize)
{
    TaskContext *userCtx = NULL;
    UINT8 *userStack = (UINT8 *)userStackBase;

    if ((userStack == NULL) ||
        (userStackSize < sizeof(TaskContext) + BOX_USRCTX_FP_TAIL + sizeof(UINTPTR))) {
        return NULL;
    }

    userCtx = (TaskContext *)(userStack + userStackSize - BOX_USRCTX_FP_TAIL - sizeof(TaskContext));
    (VOID)memset_s((UINT8 *)userCtx - BOX_USRCTX_FP_GAP,
                   BOX_USRCTX_FP_GAP + sizeof(TaskContext) + BOX_USRCTX_FP_TAIL, 0,
                   BOX_USRCTX_FP_GAP + sizeof(TaskContext) + BOX_USRCTX_FP_TAIL);

    userCtx->uwPriMask = 0;
    userCtx->uwR0 = taskCB->taskId;
    /* LR = exit trampoline, thumb bit forced: the task returns through a
     * direct bx lr, and an even target raises an INVSTATE usage fault.
     * Without this a returning entry function would branch to 0. */
    userCtx->uwLR = (UINT32)(UINTPTR)OsUsrTaskExit | 1;
    userCtx->uwPC = (UINT32)(UINTPTR)taskCB->taskEntry;
    userCtx->uwxPSR = 0x01000000U;           /* thumb */

    return (VOID *)((UINT8 *)userCtx - BOX_USRCTX_FP_GAP);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
