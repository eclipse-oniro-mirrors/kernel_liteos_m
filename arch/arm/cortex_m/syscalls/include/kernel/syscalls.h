#ifndef _SYSCALL_H
#define _SYSCALL_H

/*
 * BOX syscall rename - kernel side. Same shape and location as refs
 * arch_s/arm/cortex_m/syscalls/include/kernel/syscalls.h.
 *
 * Reached via los_base.h (`#ifdef LOSCFG_BOX` + `__has_include`):
 * kernel compiles see
 *   LOS_xxx  ->  SYS_LOS_xxx
 * for every syscall-exposed API. The kernel implementation (defined in
 * kernel sources as LOS_xxx) and ALL internal call sites become direct
 * references to the SYS_ implementation - kernel threads call it
 * directly, no svc, no veneer.
 *
 * User code never wants this mapping: box usr modules compile with the
 * syscalls/include/usr root only, which carries no kernel/ subdir, so
 * the __has_include in los_base.h is FALSE there and this header is
 * unreachable from the user world.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef LOS_DYN_SYSCALL_LIB
#define PREFIX DYNSYSCALL
#else
#define PREFIX(f) SYS_##f
#endif

/* task system call */
#define LOS_TaskDelay                 PREFIX(LOS_TaskDelay)
#define LOS_PrintStr                  PREFIX(LOS_PrintStr)
#define LOS_TaskUsrExit               PREFIX(LOS_TaskUsrExit)
#define LOS_OffsetTest                PREFIX(LOS_OffsetTest)
/* M5G-R: the uart output syscall of the common-copy PRINTK, mux shape:
 * the rename turns the board driver's definition and every kernel caller
 * into SYS_UartPuts (the dispatch-table slot references it directly - no
 * wrapper); the plain name stays free for the COMMON_TEXT veneer serving
 * user callers. */
#define UartPuts                      PREFIX(UartPuts)

/* M5D: task management surface (refs TASK_BASE block). All are syscalls
 * except LOS_TaskCreateOnly / LOS_TaskCreate / LOS_TaskStackRecycle which
 * are usr-world wrappers (refs los_usr_api.c) - they are renamed so the
 * kernel definitions resolve to SYS_ and the plain names stay free for the
 * box usr copies (single-source COMMON_TEXT shape). */
#define LOS_TaskPriGet                PREFIX(LOS_TaskPriGet)
#define LOS_TaskYield                 PREFIX(LOS_TaskYield)
#define LOS_TaskLock                  PREFIX(LOS_TaskLock)
#define LOS_TaskUnlock                PREFIX(LOS_TaskUnlock)
#define LOS_TaskDelete                PREFIX(LOS_TaskDelete)
#define LOS_TaskResume                PREFIX(LOS_TaskResume)
#define LOS_TaskSuspend               PREFIX(LOS_TaskSuspend)
#define LOS_TaskPriSet                PREFIX(LOS_TaskPriSet)
#define OsTaskCreateOnlyDynamic       PREFIX(OsTaskCreateOnlyDynamic)
#define OsTaskGetRecycleStack         PREFIX(OsTaskGetRecycleStack)
#define LOS_TaskCreateOnly            PREFIX(LOS_TaskCreateOnly)
#define LOS_TaskCreate                PREFIX(LOS_TaskCreate)
#define LOS_TaskStackRecycle          PREFIX(LOS_TaskStackRecycle)
/* M5G task query/static family (refs TASK_BASE+1/5/9/10/15). 5/9/10 are the
 * OHOS natives (the rename makes kernel definitions/callers resolve to
 * SYS_ and leaves the plain name for the COMMON_TEXT veneer); 1/15 are the
 * BOX-only static-stack shims in los_task_box.c. */
#define LOS_TaskCreateStatic          PREFIX(LOS_TaskCreateStatic)
#define LOS_TaskCreateOnlyStatic      PREFIX(LOS_TaskCreateOnlyStatic)
#define LOS_CurTaskPriSet             PREFIX(LOS_CurTaskPriSet)
#define LOS_TaskInfoGet               PREFIX(LOS_TaskInfoGet)
#define LOS_TaskStatusGet             PREFIX(LOS_TaskStatusGet)

/* M5C mutex (refs syscalls_def.h IPC block). LOS_MuxCreate carries no
 * veneer (the box usr world defines it, refs los_usr_api.c shape); the
 * rename still applies so kernel definitions/callers resolve to the
 * SYS_ implementation and the plain symbol stays free for the box usr
 * copy. */
#define OsMuxCreate                   PREFIX(OsMuxCreate)
#define LOS_MuxDelete                 PREFIX(LOS_MuxDelete)
#define LOS_MuxPend                   PREFIX(LOS_MuxPend)
#define LOS_MuxPost                   PREFIX(LOS_MuxPost)
#define LOS_MuxHeld                   PREFIX(LOS_MuxHeld)
#define LOS_MuxCreate                 PREFIX(LOS_MuxCreate)

/* M5G sem block: same rename shape as the mux block. The SYS_ prototypes
 * are declared alongside the renames: los_sem.h reaches some TUs through
 * include chains that complete BEFORE this header, so their bare
 * prototypes never match the renamed call sites (the TickCountGet trap);
 * the local declarations make every renamed TU self-sufficient. */
#define LOS_SemCreate                PREFIX(LOS_SemCreate)
#define LOS_SemDelete                PREFIX(LOS_SemDelete)
#define LOS_SemPend                   PREFIX(LOS_SemPend)
#define LOS_SemPost                   PREFIX(LOS_SemPost)
UINT32 SYS_LOS_SemCreate(UINT16 count, UINT32 *semHandle);
UINT32 SYS_LOS_SemDelete(UINT32 semHandle);
UINT32 SYS_LOS_SemPend(UINT32 semHandle, UINT32 timeout);
UINT32 SYS_LOS_SemPost(UINT32 semHandle);

/* M5C/M5D memory: the bestfit allocator now lives ONCE in the shared
 * COMMON sections (box_usr_mem + lds file-name routing), so NO renames
 * and NO alloc/free syscalls — kernel and boxes call the same plain
 * LOS_MemAlloc/AllocAlign/Free directly; only the pool locks go through
 * the mux veneers. The one mem syscall is LOS_MemInit (OTHER_BASE+3):
 * the table entry references the plain common-section symbol. */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SYSCALL_H */
