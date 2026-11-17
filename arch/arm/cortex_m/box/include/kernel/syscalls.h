#ifndef _SYSCALL_H
#define _SYSCALL_H

/*
 * BOX syscall rename - kernel side.
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
 * box/include/usr root only, which carries no kernel/ subdir, so
 * the __has_include in los_base.h is FALSE there and this header is
 * unreachable from the user world.
 *
 * Feature blocks are config-gated (refs kernel/syscalls.h shape): each
 * gate must match the defining module's BUILD.gn gate AND the same block
 * in syscalls_def.h - the rename, the def entry (veneer + table slot +
 * usr prototype) and the body must appear and disappear together, or a
 * trimmed config links against missing SYS_ bodies.
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
/* The uart output syscall of the common-copy PRINTK, mux shape:
 * the rename turns the board driver's definition and every kernel caller
 * into SYS_UartPuts (the dispatch-table slot references it directly - no
 * wrapper); the plain name stays free for the COMMON_TEXT veneer serving
 * user callers. */
#define UartPuts                      PREFIX(UartPuts)

/* Task management surface. All are syscalls except LOS_TaskCreateOnly /
 * LOS_TaskCreate / LOS_TaskStackRecycle which are usr-world wrappers -
 * they are renamed so the kernel definitions resolve to SYS_ and the
 * plain names stay free for the box usr copies (single-source COMMON_TEXT
 * shape). */
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
/* Task query/static family. The plain kernel APIs are renamed so kernel
 * definitions/callers resolve to SYS_ and the plain names stay free for
 * the COMMON_TEXT veneers; LOS_TaskCreateStatic / LOS_TaskCreateOnlyStatic
 * are the BOX-only static-stack shims in los_task_box.c. */
#define LOS_TaskCreateStatic          PREFIX(LOS_TaskCreateStatic)
#define LOS_TaskCreateOnlyStatic      PREFIX(LOS_TaskCreateOnlyStatic)
#define LOS_CurTaskPriSet             PREFIX(LOS_CurTaskPriSet)
#define LOS_TaskInfoGet               PREFIX(LOS_TaskInfoGet)
#define LOS_TaskStatusGet             PREFIX(LOS_TaskStatusGet)
#ifdef LOSCFG_BASE_IPC_EVENT
/* SID task-family events (swtmr usr wake-up primitive; kernel
 * definition in kernel/base/los_event_sid.c renames to SYS_, the plain
 * name stays for the COMMON_TEXT veneer). */
#define LOS_TaskEventRead             PREFIX(LOS_TaskEventRead)
#define LOS_TaskEventWrite            PREFIX(LOS_TaskEventWrite)
#endif
#ifdef LOSCFG_BASE_CORE_SWTMR
/* Swtmr usr surface. LOS_SwtmrCreate/Start/Stop/Delete renames the
 * kernel definitions in los_swtmr.c into SYS_ (the dispatch slots take
 * them directly); OsSwtmrHandlerItemGet / OsSwtmrUsrTaskInit rename the
 * BOX swtmr helpers in los_swtmr_usr.c. The plain names stay free for the
 * COMMON_TEXT veneers serving the box world. */
#define LOS_SwtmrCreate               PREFIX(LOS_SwtmrCreate)
#define LOS_SwtmrStart                PREFIX(LOS_SwtmrStart)
#define LOS_SwtmrStop                 PREFIX(LOS_SwtmrStop)
#define LOS_SwtmrDelete               PREFIX(LOS_SwtmrDelete)
#define OsSwtmrHandlerItemGet         PREFIX(OsSwtmrHandlerItemGet)
#define OsSwtmrUsrTaskInit            PREFIX(OsSwtmrUsrTaskInit)
#endif
#ifdef LOSCFG_BASE_IPC_EVENT
/* SID event-pool family (kernel definitions in los_event_sid.c). */
#define LOS_EventCreate               PREFIX(LOS_EventCreate)
#define LOS_EventDelete               PREFIX(LOS_EventDelete)
#define LOS_EventReadWithSId          PREFIX(LOS_EventReadWithSId)
#define LOS_EventWriteWithSId         PREFIX(LOS_EventWriteWithSId)
#define LOS_EventPollWithSId          PREFIX(LOS_EventPollWithSId)
#define LOS_EventClearWithSId         PREFIX(LOS_EventClearWithSId)
#define LOS_EventWriteOnceWithSId     PREFIX(LOS_EventWriteOnceWithSId)
#endif
#ifdef LOSCFG_BASE_IPC_QUEUE
/* Queue surface (kernel definitions in los_queue.c rename to SYS_;
 * in-tree kernel callers such as cmsis/lwip keep working either way - the
 * veneer tail-calls SYS_ when g_sysMode says kernel). */
#define LOS_QueueCreate               PREFIX(LOS_QueueCreate)
#define LOS_QueueDelete               PREFIX(LOS_QueueDelete)
#define LOS_QueueWriteCopy            PREFIX(LOS_QueueWriteCopy)
#define LOS_QueueReadCopy             PREFIX(LOS_QueueReadCopy)
#endif

#ifdef LOSCFG_BASE_IPC_MUX
/* Mutex block. LOS_MuxCreate carries no veneer (the box usr world
 * defines it); the rename still applies so kernel definitions/callers
 * resolve to the SYS_ implementation and the plain symbol stays free for
 * the box usr copy. */
#define OsMuxCreate                   PREFIX(OsMuxCreate)
#define LOS_MuxDelete                 PREFIX(LOS_MuxDelete)
#define LOS_MuxPend                   PREFIX(LOS_MuxPend)
#define LOS_MuxPost                   PREFIX(LOS_MuxPost)
#define LOS_MuxHeld                   PREFIX(LOS_MuxHeld)
#define LOS_MuxCreate                 PREFIX(LOS_MuxCreate)
#endif

#ifdef LOSCFG_BASE_IPC_SEM
/* Semaphore block: same rename shape as the mux block. The SYS_ prototypes
 * are declared alongside the renames: los_sem.h reaches some TUs through
 * include chains that complete BEFORE this header, so their bare
 * prototypes never match the renamed call sites (the TickCountGet trap);
 * the local declarations make every renamed TU self-sufficient. */
#define LOS_SemCreate                PREFIX(LOS_SemCreate)
#define LOS_SemDelete                PREFIX(LOS_SemDelete)
#define LOS_SemPend                   PREFIX(LOS_SemPend)
#define LOS_SemPost                   PREFIX(LOS_SemPost)
#endif

#ifdef LOSCFG_BASE_IPC_CONDVAR
/* Condvar block, refs kernel/syscalls.h shape: the four entries keep their
 * refs kernel names (refs has no LOS_Condvar* public API and no usr-world
 * wrapper), so the rename is what frees the plain symbols for the COMMON_TEXT
 * veneers. The SYS_ prototypes are declared alongside for the same reason the
 * sem block gives above (the TickCountGet trap). */
#define OsCondVarCreate               PREFIX(OsCondVarCreate)
#define OsCondVarDelete               PREFIX(OsCondVarDelete)
#define OsCondVarSignal               PREFIX(OsCondVarSignal)
#define OsCondVarWait                 PREFIX(OsCondVarWait)
extern UINT32 OsCondVarCreate(UINT32 *sId);
extern UINT32 OsCondVarDelete(UINT32 sId);
extern UINT32 OsCondVarSignal(UINT32 sId, BOOL isBroadcast);
extern UINT32 OsCondVarWait(UINT32 sId, UINT32 mutexId, UINT32 absTicks);
#endif

/* Memory: the bestfit allocator lives ONCE in the shared COMMON sections
 * (box_usr_mem + lds file-name routing), so NO renames and NO alloc/free
 * syscalls — kernel and boxes call the same plain
 * LOS_MemAlloc/AllocAlign/Free directly; only the pool locks go through
 * the mux veneers. The one mem syscall is LOS_MemInit: its table entry
 * references the plain common-section symbol. */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SYSCALL_H */
