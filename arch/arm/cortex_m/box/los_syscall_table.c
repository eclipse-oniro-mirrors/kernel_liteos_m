/*
 * los_syscall_table.c — syscall dispatch table + kernel-side wrappers.
 * Generated from syscalls_def.h (single source with the veneer asm and
 * the user prototypes). Table lives in kernel space, invisible to user.
 * The uart driver prototypes below stay extern: arch code must not include
 * board headers.
 */
#include "los_typedef.h"
#include "los_task.h"        /* LOS_TaskDelay decl; the los_base.h rename
                              * maps it to SYS_LOS_TaskDelay for the table */
#include "los_task_pri.h"    /* OsCurrTaskGet, LosTaskCB (wrappers) */
#include "los_error.h"
#include "los_box_pri.h"
#include "los_mux.h"         /* LOS_MuxDelete/Pend/Post (-> SYS_*) */
#include "los_mux_pri.h"     /* OsMuxCreate / LOS_MuxHeld contracts */
#include "los_memory.h"      /* LOS_MemAlloc/AllocAlign/Free (-> SYS_*) */
#include "los_sem.h"         /* LOS_SemCreate/Delete/Pend/Post (-> SYS_*) */
#include "los_event_sid.h"   /* LOS_TaskEventRead/Write, event pool family */
#include "los_queue.h"        /* LOS_QueueCreate/Delete/Read+WriteCopy (-> SYS_*) */
#include "los_swtmr.h"        /* LOS_SwtmrCreate/... + BOX section types */
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
#include "los_swtmr_usr_pri.h" /* OsSwtmrHandlerItemGet/UsrTaskInit prototypes */
#endif
#include "syscalls_num.h"

/* Board uart driver (targets/<board>/liteos_m/board/driver/uart.c):
 * prototypes only - the extern keeps this TU free of board includes.
 * Under the los_base.h rename this declares SYS_UartPuts (the uart.c
 * definition renames with it - mux shape); the dispatch slot below takes
 * the renamed symbol directly, so this entry carries no wrapper. */
extern VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock);
extern INT32 UartPutc(INT32 c, VOID *file);

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Table-slot placeholder type ONLY - the dispatch chain never calls
 * through it: the svc graft writes fn into the graft frame's
 * pc slot and bx EXC_RETURNs, so the implementation starts executing as a
 * thread entry and receives args per its own C prototype: 1-4 from the
 * hardware frame r0-r3 slots, 5-8 from the AAPCS stack staging. Eight
 * UINTPTR params match that graft capacity; an API taking more than eight
 * arguments would need a struct-passing protocol instead. */
typedef UINTPTR (*SYSCALL_FUNC)(UINTPTR, UINTPTR, UINTPTR, UINTPTR,
                                UINTPTR, UINTPTR, UINTPTR, UINTPTR);

typedef struct {
    SYSCALL_FUNC fn;
    UINT32 perm;             /* per-box permission bitmask */
} SyscallEntry;

#define SYSCALL_PERM_ALL 0xFFU

/* kernel-side wrappers, defined below; the los_base.h rename turns both
 * the declarations and the definitions into SYS_LOS_xxx */
UINT32 LOS_PrintStr(const CHAR *str);
UINT32 LOS_TaskUsrExit(UINT32 ret);
UINT32 LOS_OffsetTest(UINT32 a1, UINT32 a2, UINT32 a3, UINT32 a4,
                      UINT32 a5, UINT32 a6, UINT32 a7, UINT32 a8);

#define LITEOS_SYSCALL_DEF(num, ret, name, args...) \
    [num] = { (SYSCALL_FUNC)name, SYSCALL_PERM_ALL },

const SyscallEntry g_syscallTable[LOSCFG_SYSCALL_NUM] = {
#include "syscalls_def.h"
};

/* Kernel-side wrapper: user string printing with a bounded copy. Reading the
 * user pointer directly is the accepted ABI cost; the loop below caps the
 * length. */
UINT32 LOS_PrintStr(const CHAR *str)
{
    UINT32 n;

    if (str == NULL) {
        return LOS_NOK;
    }
    for (n = 0; (n < 128U) && (str[n] != '\0'); n++) {
        (VOID)UartPutc((INT32)(UINT8)str[n], NULL);
    }
    return n;
}

/* 8-arg validation: each arg carries its position value; return a magic
 * if all 8 arrive intact (r0-r3 + AAPCS stack staging from the graft
 * frame), a diagnostic otherwise. */
UINT32 LOS_OffsetTest(UINT32 a1, UINT32 a2, UINT32 a3, UINT32 a4,
                      UINT32 a5, UINT32 a6, UINT32 a7, UINT32 a8)
{
    if ((a1 == 1) && (a2 == 2) && (a3 == 3) && (a4 == 4) &&
        (a5 == 5) && (a6 == 6) && (a7 == 7) && (a8 == 8)) {
        return 0x12345678U;
    }
    return 0xDEAD0000U | ((a5 & 0xFFU) << 8) | (a8 & 0xFFU);
}

/* Kernel-side wrapper: user task termination. Reached through the
 * OsUsrTaskExit trampoline when a box entry function returns. Deletes the
 * current task; the recycle path (OsTaskStackFree) routes the TCB, the pool
 * kernel stack and - for dynamic stacks - the user stack back to the box by
 * the encoded task id, so no manual bookkeeping clearing is needed here.
 * Never returns. */
UINT32 LOS_TaskUsrExit(UINT32 ret)
{
    LosTaskCB *taskCB = OsCurrTaskGet();

    (VOID)ret;
    if (taskCB == NULL) {
        return LOS_NOK;
    }
    return LOS_TaskDelete(taskCB->taskId);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
