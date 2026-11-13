/*
 * los_box_task.c — BOX task context crafting (M5A: moved to the arch layer
 * where refs keeps OsTskUserStackInit; M5B: the crafted context is entered
 * the born-INUSER way - the task is created "as if it had been switched out
 * in user mode", so the very first PendSV dispatch lands in the entry
 * function unprivileged with psp on the user stack).
 *
 * PARKED (pitfall 41): the refs first-dispatch flavor (OsUserTaskEntry
 * kernel-mode prelude + OsUserspaceEnter descent, kept below and compiled in
 * los_box_uspace.S) crashed on the first BLOCKING syscall with
 * UNDEFINSTR-at-garbage-PC (QEMU, CFSR=0x40000) - interaction with the
 * phase-1 graft/epilogue machinery not root-caused. The born-INUSER entry is
 * the QEMU+board-validated equivalent and stays the mechanism until a
 * dedicated milestone cracks the descent.
 *
 * Context geometry (M4H unified 204B, from the user stack pointer):
 *   [sp+0  ] 64B gap (d8-d15 area - zeroed: fresh-task FP state defined)
 *   [sp+64 ] TaskContext: r4-r12 + priMask (36B, zeroed)
 *   [sp+100] HW frame r0..xPSR (32B)
 *   [sp+132] FP area S0-S15+FPSCR+pad (72B, popped by an extended return)
 * The first dispatch restores r4-r12 from the zeroed gap (dead registers,
 * primask slot reads 0) and pops the extended frame - S regs defined.
 */
#include "los_typedef.h"
#include "los_task_pri.h"
#include "los_arch_context.h"
#include "securec.h"
#include "los_box_pri.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* User task exit trampoline (COMMON_FLASH): a box entry function that
 * simply returns lands here and terminates via LOS_TaskUsrExit. */
extern VOID OsUsrTaskExit(VOID);

/*
 * Craft the initial user context on the box stack (phase-1 geometry and
 * semantics): PC = the real entry, LR = the exit trampoline, INUSER set by
 * the caller. Returns the user stack pointer.
 */
VOID *OsBoxTskUserStackInit(LosTaskCB *taskCB, VOID *userStackBase, UINT32 userStackSize)
{
    TaskContext *userCtx = NULL;
    UINT8 *userStack = (UINT8 *)userStackBase;

    if ((userStack == NULL) || (userStackSize < sizeof(TaskContext) + 72 + sizeof(UINTPTR))) {
        return NULL;
    }

    userCtx = (TaskContext *)(userStack + userStackSize - 72 - sizeof(TaskContext));
    (VOID)memset_s((UINT8 *)userCtx - 64, 64 + sizeof(TaskContext) + 72, 0,
                   64 + sizeof(TaskContext) + 72);

    userCtx->uwPriMask = 0;
    userCtx->uwR0 = taskCB->taskId;
    /* LR = exit trampoline, thumb bit forced (refs task.c OsTskUserStackInit:
     * the task returns through a direct bx lr; an even target raises
     * INVSTATE). Without this a returning entry function would branch to 0. */
    userCtx->uwLR = (UINT32)(UINTPTR)OsUsrTaskExit | 1;
    userCtx->uwPC = (UINT32)(UINTPTR)taskCB->taskEntry;
    userCtx->uwxPSR = 0x01000000U;           /* thumb */

    return (VOID *)((UINT8 *)userCtx - 64 /* d8-d15 gap */);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
