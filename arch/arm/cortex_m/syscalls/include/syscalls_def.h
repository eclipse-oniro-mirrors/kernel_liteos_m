#ifndef SYSCALLS_DEF_H
#define SYSCALLS_DEF_H

#if !defined(LITEOS_SYSCALL_DEF)
#error "define LITEOS_SYSCALL_DEF before including syscalls_def.h"
#endif

/* Numbering is an ABI (M5 dynload compiles box images separately):
 * 0-63 TASK, 65+ IPC, 130+ OTHER, 170+ EXTENDED, 240-255 reserved.
 *
 * Single form, refs style (LITEOS_SYSCALL_DEF(num, ret, name, args...)).
 * The name field is the USER-FACING API name; the layers resolve it:
 *   kernel implementation = SYS_<name>  (los_base.h -> kernel/syscalls.h
 *                                         renames the LOS_xxx definition
 *                                         and every kernel call site)
 *   user veneer            = <name>     (COMMON_TEXT, plain symbol)
 *   user prototype         = <name>     (usr/syscalls.h, after #undef)
 */

LITEOS_SYSCALL_DEF(1, UINT32, LOS_TaskDelay, UINT32 tick)

LITEOS_SYSCALL_DEF(2, UINT32, LOS_PrintStr, const CHAR *str)

/* 8-arg AAPCS stack-pass validation (M4C-2 graft frame test): args 1-4
 * arrive in r0-r3 (hardware frame), args 5-8 at [SP, SP+16) (the entry
 * stages them from the user stack into the graft frame). If the staging
 * is wrong, args 5-8 read garbage and the check fails. */
LITEOS_SYSCALL_DEF(3, UINT32, LOS_OffsetTest,
                   UINT32 a1, UINT32 a2, UINT32 a3, UINT32 a4,
                   UINT32 a5, UINT32 a6, UINT32 a7, UINT32 a8)

/* User task termination (refs TASK_BASE+36): the entry function returned
 * through the OsUsrTaskExit trampoline (its LR), so the kernel deletes the
 * current task and clears the box bookkeeping on the recycled TCB. */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_USR_EXIT, UINT32, LOS_TaskUsrExit, UINT32 ret)

#endif
