#ifndef SYSCALLS_DEF_H
#define SYSCALLS_DEF_H

#if !defined(LITEOS_SYSCALL_DEF)
#error "define LITEOS_SYSCALL_DEF before including syscalls_def.h"
#endif

/* Numbering is an ABI (dynload compiles box images separately, so every
 * number here is bound at the def site and must stay stable):
 * 0-63 TASK, 65+ IPC, 130+ OTHER, 170+ EXTENDED, 240-255 reserved.
 *
 * Single form (LITEOS_SYSCALL_DEF(num, ret, name, args...)); the num field
 * is always one of the SYSCALL_NUM_* macros from syscalls_num.h, never a
 * bare literal. The name field is the USER-FACING API name; the layers
 * resolve it:
 *   kernel implementation = SYS_<name>  (los_base.h -> kernel/syscalls.h
 *                                         renames the LOS_xxx definition
 *                                         and every kernel call site)
 *   user veneer            = <name>     (COMMON_TEXT, plain symbol)
 *   user prototype         = <name>     (usr/syscalls.h, after #undef)
 *
 * Feature blocks are config-gated (refs shape). The SAME gate wraps the
 * rename block in kernel/syscalls.h: rename, def entry (veneer + table
 * slot + usr prototype) and the defining module's BUILD.gn source gate
 * must appear and disappear together.
 */

/* Delay: its number must never collide with another live entry — the
 * dispatch table has exactly one slot per number, so a collision silently
 * hijacks an entire API and every call to the loser routes into the
 * winner's body (a delay sharing the TaskCreateStatic slot would run the
 * create shim and return without sleeping; callers ignore the error code,
 * so it fails silently). */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_DELAY, UINT32, LOS_TaskDelay, UINT32 tick)

/* Demo syscalls live in the OTHER segment (130+); the mem surface consumes
 * no numbers there at all (zero mem syscalls - the shared COMMON allocator
 * is a direct call). */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_PRINTSTR, UINT32, LOS_PrintStr, const CHAR *str)

/* 8-arg AAPCS stack-pass validation (graft frame test): args 1-4
 * arrive in r0-r3 (hardware frame), args 5-8 at [SP, SP+16) (the entry
 * stages them from the user stack into the graft frame). If the staging
 * is wrong, args 5-8 read garbage and the check fails. */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_OFFSETTEST, UINT32, LOS_OffsetTest,
                   UINT32 a1, UINT32 a2, UINT32 a3, UINT32 a4,
                   UINT32 a5, UINT32 a6, UINT32 a7, UINT32 a8)

/* The uart output syscall - the output hop of the COMMON-copy
 * PRINTK (los_printf.c in box_usr_mem). The driver itself (and its
 * LOS_IntLock -> ArchIntLock) stays in the kernel segment; user code
 * reaches it only through this entry. Same direct-dereference ABI cost
 * as LOS_PrintStr (bounded by the caller's len). */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_UARTPUTS, VOID, UartPuts,
                   const CHAR *s, UINT32 len, BOOL isLock)

/* User task termination: the entry function returned
 * through the OsUsrTaskExit trampoline (its LR), so the kernel deletes the
 * current task and clears the box bookkeeping on the recycled TCB. */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_USR_EXIT, UINT32, LOS_TaskUsrExit, UINT32 ret)

/* ---- Task management surface (TASK-segment numbering) ----
 * All use the box-aware OS_TASK_ID_CHECK_INVALID seam (los_task_pri.h),
 * so box task ids (encoded) resolve through OS_TCB_FROM_TID.
 * The plain entries dispatch straight to the kernel natives; the two
 * static-stack creates are BOX-only shims in los_task_box.c (a param-check
 * subset plus the generic OsTaskCreateOnly path with the caller-supplied
 * topStack) — gated, BOX=off builds drop them. */
#ifdef LOSCFG_BOX
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_CREATE_STATIC, UINT32, LOS_TaskCreateStatic,
                   UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack)
#endif
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_PRIGET, UINT16, LOS_TaskPriGet, UINT32 taskId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_CURPRISET, UINT32, LOS_CurTaskPriSet, UINT16 taskPrio)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_YIELD, UINT32, LOS_TaskYield, VOID)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_LOCK, VOID, LOS_TaskLock, VOID)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_UNLOCK, VOID, LOS_TaskUnlock, VOID)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_INFOGET, UINT32, LOS_TaskInfoGet,
                   UINT32 taskId, TSK_INFO_S *taskInfo)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_STATUSGET, UINT32, LOS_TaskStatusGet,
                   UINT32 taskId, UINT32 *taskStatus)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_DELETE, UINT32, LOS_TaskDelete, UINT32 taskId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_RESUME, UINT32, LOS_TaskResume, UINT32 taskId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_SUSPEND, UINT32, LOS_TaskSuspend, UINT32 taskId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_PRISET, UINT32, LOS_TaskPriSet, UINT32 taskId, UINT16 taskPrio)
#ifdef LOSCFG_BOX
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_CREATEONLY_STATIC, UINT32, LOS_TaskCreateOnlyStatic,
                   UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack)
#endif
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_CREATEONLY_DYN, UINT32, OsTaskCreateOnlyDynamic,
                   UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *usrTopStack)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_GET_RECYCLE_STK, VOID *, OsTaskGetRecycleStack, VOID)

/* ---- Mutex, IPC-segment numbering (SID-based API) ----
 * LOS_MuxCreate is NOT itself a syscall entry: the box usr world wraps
 * OsMuxCreate with the default attr. */
#ifdef LOSCFG_BASE_IPC_MUX
LITEOS_SYSCALL_DEF(SYSCALL_NUM_MUX_CREATE, UINT32, OsMuxCreate, UINT32 *sId, UINT32 attr)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_MUX_DELETE, UINT32, LOS_MuxDelete, UINT32 sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_MUX_PEND, UINT32, LOS_MuxPend, UINT32 sId, UINT32 timeout)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_MUX_POST, UINT32, LOS_MuxPost, UINT32 sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_MUX_HELD, UINT32, LOS_MuxHeld, UINT32 sId)
#endif

/* ---- Semaphore block (free IPC-segment slots) ----
 * Box tasks create semaphores in their own pool (los_sem_box.c infrastructure,
 * mux-face shape) and address them by the flat SID; the los_sem.c entry seams
 * resolve sId -> CB (OsSemGetCB) under BOX and keep the native index
 * path otherwise — so BOX=off table entries bind the renamed kernel natives
 * with unchanged behaviour. */
#ifdef LOSCFG_BASE_IPC_SEM
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SEM_CREATE, UINT32, LOS_SemCreate, UINT16 count, UINT32 *sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SEM_DELETE, UINT32, LOS_SemDelete, UINT32 sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SEM_PEND, UINT32, LOS_SemPend, UINT32 sId, UINT32 timeout)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SEM_POST, UINT32, LOS_SemPost, UINT32 sId)
#endif

/* ---- SID task-family events (task addressing) ----
 * The event set embedded in every LosTaskCB. Read = current task
 * (read-and-clear, OR match, may block); write = target task by id,
 * wake-up primitive of the per-box swtmr task. The kernel
 * definitions live in kernel/base/los_event_sid.c and resolve to SYS_
 * through the los_base.h rename; the plain names are the COMMON_TEXT
 * veneers. */
#ifdef LOSCFG_BASE_IPC_EVENT
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_EVENT_READ, UINT32, LOS_TaskEventRead,
                   UINT32 event, UINT32 timeout)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_EVENT_WRITE, UINT32, LOS_TaskEventWrite,
                   UINT32 taskId, UINT32 event)
#endif

/* ---- swtmr usr ----
 * Timer API for the box world: ids are OS_BOX_SET_ID-encoded (kernel pool
 * ids stay plain). The two Os* entries serve the common-section swtmr
 * task (handler consumption) and the user-side bring-up (BoxSwtmrInit). */
#ifdef LOSCFG_BASE_CORE_SWTMR
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SWTMR_CREATE, UINT32, LOS_SwtmrCreate,
                   UINT32 interval, UINT8 mode, SWTMR_PROC_FUNC handler, UINT32 *swtmrID, UINTPTR arg)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SWTMR_START, UINT32, LOS_SwtmrStart, UINT32 swtmrId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SWTMR_STOP, UINT32, LOS_SwtmrStop, UINT32 swtmrId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SWTMR_DELETE, UINT32, LOS_SwtmrDelete, UINT32 swtmrId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SWTMR_HANDLER_GET, UINT32, OsSwtmrHandlerItemGet,
                   SwtmrHandlerItem *item)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SWTMR_USR_TASK_INIT, UINT32, OsSwtmrUsrTaskInit,
                   UINT16 boxId, UINT32 stackSize)
/* refs parity: remaining two entries of the refs swtmr user surface. */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SWTMR_TIMEGET, UINT32, LOS_SwtmrTimeGet,
                   UINT32 swtmrId, UINT32 *tick)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SWTMR_USR_INIT, UINT32, LOS_SwtmrUsrInit,
                   VOID *taskStack, UINT32 stackSize)
#endif

/* ---- tick readouts (refs parity arms TASK+20 / TASK+22) ----
 * CurrNanosec returns UINT64 (r0/r1): the kernel epilogue writes both words
 * back into the veneer's args block and the veneer's pop {r0-r3} restores
 * them, so the 64-bit value crosses the trap intact. GetCpuCycle writes
 * through user-supplied pointers (direct-dereference ABI, same accepted
 * cost as PrintStr). */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_CURR_NANOSEC, UINT64, LOS_CurrNanosec, VOID)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_GET_CPU_CYCLE, VOID, LOS_GetCpuCycle,
                   UINT32 *cntHi, UINT32 *cntLo)

/* ---- SID event pool (BOX event IPC) ----
 * Pool-backed events addressed by flat sId (the owning box's pool).
 * The kernel definitions live in kernel/box/los_event_sid.c and resolve
 * to SYS_ through the los_base.h rename. */
#ifdef LOSCFG_BASE_IPC_EVENT
LITEOS_SYSCALL_DEF(SYSCALL_NUM_EVENT_CREATE, UINT32, LOS_EventCreate, UINT32 *sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_EVENT_DELETE, UINT32, LOS_EventDelete, UINT32 sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_EVENT_READ_SID, UINT32, LOS_EventReadWithSId,
                   UINT32 sId, UINT32 eventMask, UINT32 mode, UINT32 timeout)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_EVENT_WRITE_SID, UINT32, LOS_EventWriteWithSId,
                   UINT32 sId, UINT32 events)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_EVENT_POLL_SID, UINT32, LOS_EventPollWithSId,
                   UINT32 sId, UINT32 eventMask, UINT32 mode)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_EVENT_CLEAR_SID, UINT32, LOS_EventClearWithSId,
                   UINT32 sId, UINT32 events)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_EVENT_WRITE_ONCE_SID, UINT32, LOS_EventWriteOnceWithSId,
                   UINT32 sId, UINT32 events)
#endif

/* ---- queue (SID-bound handles) ----
 * Kernel definitions live in los_queue.c and resolve to SYS_ through the
 * los_base.h rename; the plain names are the COMMON_TEXT veneers. */
#ifdef LOSCFG_BASE_IPC_QUEUE
LITEOS_SYSCALL_DEF(SYSCALL_NUM_QUEUE_CREATE, UINT32, LOS_QueueCreate,
                   const CHAR *queueName, UINT16 len, UINT32 *queueID, UINT32 flags,
                   UINT16 maxMsgSize)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_QUEUE_DELETE, UINT32, LOS_QueueDelete, UINT32 queueID)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_QUEUE_WRITE_COPY, UINT32, LOS_QueueWriteCopy,
                   UINT32 queueID, VOID *bufferAddr, UINT32 bufferSize, UINT32 timeOut)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_QUEUE_READ_COPY, UINT32, LOS_QueueReadCopy,
                   UINT32 queueID, VOID *bufferAddr, UINT32 *bufferSize, UINT32 timeOut)
#endif

/* ---- condvar (BOX condition variable, refs safe_ipc/los_condvar.c) ----
 * Four entries, exposed under their refs kernel names (OsCondVarXxx) rather
 * than LOS_Condvar*: refs has no public los_condvar.h and no usr-world
 * wrapper, its kernel/syscalls.h renames the OsCondVarXxx symbols directly.
 * OsCondVarCreate's sId argument is IN/OUT - pass 0 to have one allocated. */
#ifdef LOSCFG_BASE_IPC_CONDVAR
LITEOS_SYSCALL_DEF(SYSCALL_NUM_COND_CREATE, UINT32, OsCondVarCreate, UINT32 *sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_COND_DELETE, UINT32, OsCondVarDelete, UINT32 sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_COND_SIGNAL, UINT32, OsCondVarSignal, UINT32 sId, BOOL isBroadcast)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_COND_WAIT, UINT32, OsCondVarWait,
                   UINT32 sId, UINT32 mutexId, UINT32 absTicks)
#endif

/* ---- memory: zero mem syscalls ----
 * The allocator (bestfit + dlink helper) lives ONCE in the shared COMMON
 * sections (usr_demo:box_usr_mem + lds file-name routing), so every mem
 * entry — alloc/free/align AND LOS_MemInit — is a DIRECT call from the
 * box world; only the pool LOCKS go through the mux syscalls (75..79).
 * LOSCFG_MEM_MUL_POOL must stay OFF: the multipool registry is a single
 * global structure that the overlap walk reads through the kernel heap
 * header — kernel RAM, unreachable from user mode — so a shared single
 * copy of the allocator cannot share one registry per box. With the
 * registry compiled out the box heap init touches only box-accessible
 * memory, and the whole mem surface stays direct-call. OTHER_BASE+0..39
 * therefore stay unallocated. */

#endif
