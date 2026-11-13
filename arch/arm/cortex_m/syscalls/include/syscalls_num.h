#ifndef SYSCALLS_NUM_H
#define SYSCALLS_NUM_H
/* One past the highest syscall number in syscalls_def.h (M5G-R: task static
 * + query family (1/5/9/10/15), the sem block (80..83), the demo
 * PrintStr/OffsetTest in the refs OTHER segment (130/131) and the UartPuts
 * syscall (132, the common PRINTK output path); highest live def: 132+1.) */
#define LOSCFG_SYSCALL_NUM  133

/* Numbering follows the refs syscalls_def.h class layout:
 * TASK base 0-63, IPC base 65+, OTHER base 130+.
 * 36 = refs TASK_BASE + 36 (LOS_TaskUsrExit). */
#define SYSCALL_NUM_TASK_USR_EXIT  36

/* M5G-R number fix (pitfall 50): the demo syscalls previously used the
 * literal numbers 1/2/3, colliding with refs TASK_BASE+1 (TaskCreateStatic)
 * for TaskDelay — the dispatch table had ONE entry for both, and every
 * usr delay landed in the create shim (silent no-sleep, child starvation,
 * tick window 0). Now aligned with refs: delay = TASK_BASE+2; the demo
 * PrintStr/OffsetTest sit in the refs OTHER segment (which refs leaves
 * entirely free because it has zero mem syscalls there). */
#define SYSCALL_NUM_TASK_DELAY              2
#define SYSCALL_NUM_PRINTSTR              130
#define SYSCALL_NUM_OFFSETTEST            131
#define SYSCALL_NUM_UARTPUTS              132

/* M5D/M5G: task management surface, refs TASK_BASE numbering:
 *    1  LOS_TaskCreateStatic (BOX only, shim in los_task_box.c)
 *    4  LOS_TaskPriGet      6  LOS_TaskYield       7  LOS_TaskLock
 *    8  LOS_TaskUnlock      9  LOS_TaskInfoGet    10  LOS_TaskStatusGet
 *   11  LOS_TaskDelete     12  LOS_TaskResume     13  LOS_TaskSuspend
 *   14  LOS_TaskPriSet     15  LOS_TaskCreateOnlyStatic (BOX only)
 *  36  LOS_TaskUsrExit
 *  38  OsTaskCreateOnlyDynamic  39  OsTaskGetRecycleStack
 * 5/9/10 dispatch to the OHOS natives directly (D6 one-line class). */
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

/* M5C: refs LITEOS_SYSCALL_IPC_BASE (= 65) + 10..14, the refs mux block. */
#define SYSCALL_NUM_MUX_CREATE     75
#define SYSCALL_NUM_MUX_DELETE     76
#define SYSCALL_NUM_MUX_PEND       77
#define SYSCALL_NUM_MUX_POST       78
#define SYSCALL_NUM_MUX_HELD       79

/* M5G: sem block. refs syscalls_def.h has NO sem defs (IPC_BASE+20..24 are
 * RSV there — refs sem only serves kernel/driver boxes), so these take free
 * refs IPC-segment slots; the gate stays the single-DEF form and the
 * BOX=off table entries bind the renamed OHOS natives. */
#define SYSCALL_NUM_SEM_CREATE     80
#define SYSCALL_NUM_SEM_DELETE     81
#define SYSCALL_NUM_SEM_PEND      82
#define SYSCALL_NUM_SEM_POST      83

/* M5D: refs LITEOS_SYSCALL_OTHER_BASE (= 130) + 0..39 stay free — zero
 * mem syscalls: the shared COMMON allocator is called directly, and the
 * box-heap init is a direct call too (LOSCFG_MEM_MUL_POOL is OFF, so the
 * refs multipool registry walk does not exist; see syscalls_def.h). */

#endif
