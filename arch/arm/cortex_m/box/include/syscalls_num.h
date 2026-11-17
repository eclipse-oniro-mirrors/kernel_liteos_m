#ifndef SYSCALLS_NUM_H
#define SYSCALLS_NUM_H
/* One past the highest syscall number in syscalls_def.h. Every number must
 * be unique: the dispatch table holds exactly one slot per number, so two
 * defs sharing a number silently overwrite each other and the losing API
 * ends up running the winner's body - no link error, no runtime error. */
#define LOSCFG_SYSCALL_NUM  156

/* Numbering follows the class layout:
 * TASK base 0-63, IPC base 65+, OTHER base 130+.
 * 36 = TASK_BASE + 36 (LOS_TaskUsrExit). */
#define SYSCALL_NUM_TASK_USR_EXIT  36

/* These sit in their class ranges rather than taking ad-hoc low numbers: a
 * literal number that happens to equal a class-based one collides silently
 * (see the uniqueness note above), and the failure mode is an API quietly
 * running another API's body - for example every user delay landing in the
 * task-create shim, which returns an error code no caller checks, so delays
 * simply stop sleeping. PrintStr/OffsetTest/UartPuts use the OTHER segment,
 * which is otherwise free because there are zero mem syscalls. */
#define SYSCALL_NUM_TASK_DELAY              2
#define SYSCALL_NUM_PRINTSTR              130
#define SYSCALL_NUM_OFFSETTEST            131
#define SYSCALL_NUM_UARTPUTS              132

/* Task-embedded event read/write of the SID task family - the wake-up
 * primitive of the per-box software-timer task. Appended past the OTHER
 * segment, which ends at 132. */
#define SYSCALL_NUM_TASK_EVENT_READ       133
#define SYSCALL_NUM_TASK_EVENT_WRITE      134

/* Timer API surface plus the two kernel helpers of the box swtmr task.
 * OsSwtmrHandlerItemGet is consumed by the common-section BoxSwtmrTask;
 * OsSwtmrUsrTaskInit by the user-side BoxSwtmrInit (both plain veneers into
 * the SYS_ bodies). */
#define SYSCALL_NUM_SWTMR_CREATE          135
#define SYSCALL_NUM_SWTMR_START           136
#define SYSCALL_NUM_SWTMR_STOP            137
#define SYSCALL_NUM_SWTMR_DELETE          138
#define SYSCALL_NUM_SWTMR_HANDLER_GET     139
#define SYSCALL_NUM_SWTMR_USR_TASK_INIT   140

/* SID event-pool family. */
#define SYSCALL_NUM_EVENT_CREATE           141
#define SYSCALL_NUM_EVENT_DELETE           142
#define SYSCALL_NUM_EVENT_READ_SID         143
#define SYSCALL_NUM_EVENT_WRITE_SID        144
#define SYSCALL_NUM_EVENT_POLL_SID         145
#define SYSCALL_NUM_EVENT_CLEAR_SID        146

/* Queue surface for the box world. Handles are SID-bound (the create exit
 * gate) and every entry resolves them to the owning pool's control block
 * (OsQueueResolve) - queue keeps a kernel table plus per-box pools, so a
 * handle cannot be reduced to a kernel index.
 * The Copy pair carries the payload by value; the non-Copy variants store a
 * bare pointer, which is not a meaningful cross-domain operation, so they
 * stay kernel-only. */
#define SYSCALL_NUM_QUEUE_CREATE           147
#define SYSCALL_NUM_QUEUE_DELETE           148
#define SYSCALL_NUM_QUEUE_WRITE_COPY       149
#define SYSCALL_NUM_QUEUE_READ_COPY        150

/* Write-once form of the event-pool family: OsEventWriteLockedSid with
 * once=TRUE inspects only the HEAD waiter and breaks, so at most one parked
 * task is woken instead of every matching one. Appended here rather than
 * next to 141..146 because the queue block already took those slots. */
#define SYSCALL_NUM_EVENT_WRITE_ONCE_SID   151

/* Condvar block. refs gates these behind a LITEOS_SYSCALL_IPCCOND segment
 * macro whose numbers come from a config header outside the snapshot, so the
 * four entries are appended here instead - same treatment 151 got. The kernel
 * definitions live in kernel/box/los_condvar.c and resolve to SYS_ through the
 * rename; the plain names are the COMMON_TEXT veneers. */
#define SYSCALL_NUM_COND_CREATE            152
#define SYSCALL_NUM_COND_DELETE            153
#define SYSCALL_NUM_COND_SIGNAL            154
#define SYSCALL_NUM_COND_WAIT              155

/* Task management surface, TASK_BASE numbering:
 *    1  LOS_TaskCreateStatic (BOX only, shim in los_task_box.c)
 *    4  LOS_TaskPriGet      6  LOS_TaskYield       7  LOS_TaskLock
 *    8  LOS_TaskUnlock      9  LOS_TaskInfoGet    10  LOS_TaskStatusGet
 *   11  LOS_TaskDelete     12  LOS_TaskResume     13  LOS_TaskSuspend
 *   14  LOS_TaskPriSet     15  LOS_TaskCreateOnlyStatic (BOX only)
 *  36  LOS_TaskUsrExit
 *  38  OsTaskCreateOnlyDynamic  39  OsTaskGetRecycleStack
 * 5/9/10 dispatch to the kernel natives directly. */
#define SYSCALL_NUM_TASK_CREATE_STATIC      1
#define SYSCALL_NUM_TASK_PRIGET              4
#define SYSCALL_NUM_TASK_CURPRISET           5
#define SYSCALL_NUM_TASK_YIELD               6
#define SYSCALL_NUM_TASK_LOCK                7
#define SYSCALL_NUM_TASK_UNLOCK              8
#define SYSCALL_NUM_TASK_INFOGET             9
#define SYSCALL_NUM_TASK_STATUSGET           10
#define SYSCALL_NUM_TASK_DELETE              11
#define SYSCALL_NUM_TASK_RESUME              12
#define SYSCALL_NUM_TASK_SUSPEND             13
#define SYSCALL_NUM_TASK_PRISET              14
#define SYSCALL_NUM_TASK_CREATEONLY_STATIC   15
#define SYSCALL_NUM_TASK_CREATEONLY_DYN      38
#define SYSCALL_NUM_TASK_GET_RECYCLE_STK     39

/* Mux block: LITEOS_SYSCALL_IPC_BASE (= 65) + 10..14. */
#define SYSCALL_NUM_MUX_CREATE     75
#define SYSCALL_NUM_MUX_DELETE     76
#define SYSCALL_NUM_MUX_PEND       77
#define SYSCALL_NUM_MUX_POST       78
#define SYSCALL_NUM_MUX_HELD       79

/* Sem block, taking free IPC-segment slots. The gate stays the single-DEF
 * form and the BOX=off table entries bind the renamed kernel natives. */
#define SYSCALL_NUM_SEM_CREATE     80
#define SYSCALL_NUM_SEM_DELETE     81
#define SYSCALL_NUM_SEM_PEND      82
#define SYSCALL_NUM_SEM_POST      83

/* OTHER_BASE (= 130) + 0..39 stay free apart from the demo entries above:
 * zero mem syscalls. The shared COMMON allocator is called directly, and the
 * box-heap init is a direct call too (LOSCFG_MEM_MUL_POOL is OFF, so there
 * is no multipool registry walk; see syscalls_def.h). */

#endif
