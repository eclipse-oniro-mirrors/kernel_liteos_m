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

/* M5G-R (pitfall 50): delay = refs TASK_BASE+2. The old literal 1 collided
 * with TaskCreateStatic (refs TASK_BASE+1) in the single-slot dispatch
 * table - every usr delay executed the create shim and returned without
 * sleeping (silent, callers ignore the error code). */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_DELAY, UINT32, LOS_TaskDelay, UINT32 tick)

/* Demo syscalls live in the refs OTHER segment (130+), which refs leaves
 * entirely free (zero mem syscalls - the shared COMMON allocator is a
 * direct call, M5D note). */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_PRINTSTR, UINT32, LOS_PrintStr, const CHAR *str)

/* 8-arg AAPCS stack-pass validation (M4C-2 graft frame test): args 1-4
 * arrive in r0-r3 (hardware frame), args 5-8 at [SP, SP+16) (the entry
 * stages them from the user stack into the graft frame). If the staging
 * is wrong, args 5-8 read garbage and the check fails. */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_OFFSETTEST, UINT32, LOS_OffsetTest,
                   UINT32 a1, UINT32 a2, UINT32 a3, UINT32 a4,
                   UINT32 a5, UINT32 a6, UINT32 a7, UINT32 a8)

/* M5G-R: the uart output syscall - the output hop of the COMMON-copy
 * PRINTK (los_printf.c in box_usr_mem). The driver itself (and its
 * LOS_IntLock -> ArchIntLock) stays in the kernel segment; user code
 * reaches it only through this entry. Same direct-dereference ABI cost
 * as LOS_PrintStr (phase-1 contract, bounded by the caller's len). */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_UARTPUTS, VOID, UartPuts,
                   const CHAR *s, UINT32 len, BOOL isLock)

/* User task termination (refs TASK_BASE+36): the entry function returned
 * through the OsUsrTaskExit trampoline (its LR), so the kernel deletes the
 * current task and clears the box bookkeeping on the recycled TCB. */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_TASK_USR_EXIT, UINT32, LOS_TaskUsrExit, UINT32 ret)

/* ---- M5D/M5G: task management surface (refs TASK_BASE numbering) ----
 * All use the box-aware OS_TASK_ID_CHECK_INVALID seam (los_task_pri.h),
 * so box task ids (encoded) resolve through OS_TCB_FROM_TID.
 * M5G: 5/9/10 dispatch straight to the OHOS natives (D6 one-line class);
 * 1/15 are BOX-only static-stack creates (shims in los_task_box.c:
 * refs OsUsrTaskInitParamCheck subset + the generic OsTaskCreateOnly path
 * with the caller-supplied topStack) — gated, BOX=off builds drop them. */
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

/* ---- M5C: mutex, refs syscalls_def.h IPC_BASE numbering ----
 * refs syscalls_def.h LITEOS_SYSCALL_IPC_BASE+10..14 verbatim (SID-based
 * API). LOS_MuxCreate is NOT a syscall (refs shape): the box usr world
 * wraps OsMuxCreate (refs kernel_s/usr/los_usr_api.c) with the default
 * attr. */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_MUX_CREATE, UINT32, OsMuxCreate, UINT32 *sId, UINT32 attr)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_MUX_DELETE, UINT32, LOS_MuxDelete, UINT32 sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_MUX_PEND, UINT32, LOS_MuxPend, UINT32 sId, UINT32 timeout)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_MUX_POST, UINT32, LOS_MuxPost, UINT32 sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_MUX_HELD, UINT32, LOS_MuxHeld, UINT32 sId)

/* ---- M5G: semaphore block (free refs IPC-segment slots, refs has none) ----
 * Box tasks create semaphores in their own pool (los_sem_box.c infrastructure,
 * mux-face shape) and address them by the flat SID; the los_sem.c entry seams
 * resolve sId -> CB (OsSemGetCBLocked) under BOX and keep the native index
 * path otherwise — so BOX=off table entries bind the renamed OHOS natives
 * with unchanged behaviour. */
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SEM_CREATE, UINT32, LOS_SemCreate, UINT16 count, UINT32 *sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SEM_DELETE, UINT32, LOS_SemDelete, UINT32 sId)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SEM_PEND, UINT32, LOS_SemPend, UINT32 sId, UINT32 timeout)
LITEOS_SYSCALL_DEF(SYSCALL_NUM_SEM_POST, UINT32, LOS_SemPost, UINT32 sId)

/* ---- M5D memory: refs shape, zero mem syscalls ----
 * The allocator (bestfit + dlink helper) lives ONCE in the shared COMMON
 * sections (usr_demo:box_usr_mem + lds file-name routing), so every mem
 * entry — alloc/free/align AND LOS_MemInit — is a DIRECT call from the
 * box world; only the pool LOCKS go through the mux syscalls (75..79).
 * LOSCFG_MEM_MUL_POOL is OFF (per the M5D decision): refs compiles the
 * allocator into each box image with ITS OWN pool registry, and the
 * one-copy layout cannot share one registry (the overlap walk reads the
 * kernel heap header — kernel RAM, unreachable from user mode). With the
 * registry compiled out the box heap init touches only box-accessible
 * memory, so the whole mem surface stays direct-call. OTHER_BASE+0..39
 * stay free (refs ABI-compatible). */

#endif
