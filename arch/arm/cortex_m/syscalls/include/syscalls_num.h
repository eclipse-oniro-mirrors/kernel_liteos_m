#ifndef SYSCALLS_NUM_H
#define SYSCALLS_NUM_H
/* One past the highest syscall number in syscalls_def.h. */
#define LOSCFG_SYSCALL_NUM  38

/* Numbering follows the refs syscalls_def.h class layout (TASK base):
 * 36 = refs TASK_BASE + 36 (LOS_TaskUsrExit). */
#define SYSCALL_NUM_TASK_USR_EXIT  36
#endif
