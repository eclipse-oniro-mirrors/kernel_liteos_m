/*
 * usr/syscalls.h - user-side syscall prototypes (refs location:
 * arch_s/arm/cortex_m/syscalls/include/usr/syscalls.h, which is empty
 * there; ours carries the DEF-generated prototypes).
 *
 * Include from box user code as
 *   #include "syscalls.h"
 * with syscalls/include/usr on the include path. The symbols are the
 * PLAIN LOS_xxx names, linked to the COMMON_TEXT veneers.
 */
#ifndef _USR_SYSCALLS_H
#define _USR_SYSCALLS_H

#include "los_typedef.h"

/* Undo the kernel rename (los_base.h -> kernel/syscalls.h) if this TU
 * was compiled under a kernel-world include root (should not happen -
 * this root has no kernel/ subdir, so __has_include stays FALSE - but
 * the rename must never survive into user code):
 * user code must call the VENEER, not the kernel SYS_ implementation
 * (same image, kernel flash - user access faults).
 * Box user code MUST include this header before calling any LOS_xxx
 * syscall API. Must run before the DEF-generated prototypes below. */
#undef LOS_TaskDelay
#undef LOS_PrintStr
#undef LOS_TaskUsrExit
#undef LOS_TaskPriGet
#undef LOS_TaskYield
#undef LOS_TaskLock
#undef LOS_TaskUnlock
#undef LOS_TaskDelete
#undef LOS_TaskResume
#undef LOS_TaskSuspend
#undef LOS_TaskPriSet
#undef OsTaskCreateOnlyDynamic
#undef OsTaskGetRecycleStack
#undef LOS_TaskCreateOnly
#undef LOS_TaskCreate
#undef LOS_TaskStackRecycle
#undef OsMuxCreate
#undef LOS_MuxDelete
#undef LOS_MuxPend
#undef LOS_MuxPost
#undef LOS_MuxHeld
#undef LOS_MuxCreate
#undef LOS_TaskCreateStatic
#undef LOS_TaskCreateOnlyStatic
#undef LOS_CurTaskPriSet
#undef LOS_TaskInfoGet
#undef LOS_TaskStatusGet
#undef LOS_SemCreate
#undef LOS_SemDelete
#undef LOS_SemPend
#undef LOS_SemPost
#undef UartPuts

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define LITEOS_SYSCALL_DEF(num, ret, name, ...)  ret name(__VA_ARGS__);
#include "../syscalls_def.h"

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _USR_SYSCALLS_H */
