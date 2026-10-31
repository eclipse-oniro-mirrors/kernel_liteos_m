/*
 * los_syscall_table.c — syscall dispatch table + kernel-side wrappers.
 * Generated from syscalls_def.h (single source with the veneer asm and
 * the user prototypes). Table lives in kernel space, invisible to user.
 */
#include "los_typedef.h"
#include "los_task.h"        /* LOS_TaskDelay decl; the los_base.h rename
                              * maps it to SYS_LOS_TaskDelay for the table */
#include "los_task_pri.h"    /* OsCurrTaskGet, LosTaskCB (wrappers) */
#include "los_error.h"
#include "uart.h"
#include "los_box_pri.h"
#include "syscalls_num.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef UINTPTR (*SYSCALL_FUNC)(UINTPTR, UINTPTR, UINTPTR, UINTPTR);

typedef struct {
    SYSCALL_FUNC fn;
    UINT32 perm;             /* per-box permission bitmask (M5) */
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

/* Kernel-side wrapper: user string printing with a bounded copy (phase-1: direct
 * dereference is the accepted ABI cost, boundary check below). */
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

/* Kernel-side wrapper: user task termination (refs TASK_BASE+36). Reached through the
 * OsUsrTaskExit trampoline when a box entry function returns. Deletes the
 * current task; also clears the box bookkeeping so the recycled TCB cannot
 * masquerade as a box task (a later kernel-task user of the same TCB would
 * otherwise switch the MPU to this box on dispatch). Never returns. */
UINT32 LOS_TaskUsrExit(UINT32 ret)
{
    LosTaskCB *taskCB = OsCurrTaskGet();

    (VOID)ret;
    if (taskCB == NULL) {
        return LOS_NOK;
    }
    taskCB->boxCB = NULL;
    taskCB->userStackPointer = NULL;
    taskCB->syscallNestCnt = 0;
    return LOS_TaskDelete(taskCB->taskId);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
