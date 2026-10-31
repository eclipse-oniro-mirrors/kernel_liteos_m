#ifndef ARCH_TASK_BOX_CB_H
#define ARCH_TASK_BOX_CB_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if defined(__IASMARM__) || defined(__ASSEMBLER__)
/* task TCB offsets, LosTaskCB structure */
#define TASK_CB_KERNEL_SP         0x0
#define TASK_CB_STATUS            (TASK_CB_KERNEL_SP + 0x4)
#define TASK_CB_FLAGS             (TASK_CB_STATUS + 0x4)
#define TASK_CB_STACK_SIZE        (TASK_CB_FLAGS + 0x4)
#define TASK_CB_KERNEL_TOP_SP     (TASK_CB_STACK_SIZE + 0x4)
#define TASK_CB_TASKID            (TASK_CB_KERNEL_TOP_SP + 0x4)

#define TASK_CB_USER_SP           (TASK_CB_TASKID + 0x4)

#ifdef LOSCFG_TASK_STACK_REUSE
#define TASK_CB_BOX_CB            (TASK_CB_USER_SP + 0x4)
#define TASK_CB_TOP_SP            TASK_CB_KERNEL_TOP_SP 
#else
#define TASK_CB_USER_SP_SIZE      (TASK_CB_USER_SP + 0x4)
#define TASK_CB_USER_TOP_SP       (TASK_CB_USER_SP_SIZE + 0x4)
#define TASK_CB_BOX_CB            (TASK_CB_USER_TOP_SP + 0x4)
#endif

#define TASK_CB_DRVBX_STATUS      (TASK_CB_BOX_CB + 0x4)
#define TASK_CB_CUR_BOX_ID        (TASK_CB_DRVBX_STATUS + 0x1)
#define TASK_CB_TASK_LIST         (TASK_CB_DRVBX_STATUS + 0x4)
#define TASK_CB_BACKTRACE_OFFSET (TASK_CB_TASK_LIST + 0x8)

#ifdef LOSCFG_BACKTRACE
#define TASK_CB_SYSCALL_FP        (TASK_CB_BACKTRACE_OFFSET + 0x0)
#define TASK_CB_SYSCALL_PC        (TASK_CB_SYSCALL_FP + 0x4)
#define TASK_CB_SHMEM_OFFSET      (TASK_CB_SYSCALL_PC + 0x4)
#else
#define TASK_CB_SHMEM_OFFSET      (TASK_CB_BACKTRACE_OFFSET + 0x0)
#endif

#ifdef LOSCFG_BASE_IPC_SHMEM
#define TASK_CB_SHMEM_SID         (TASK_CB_SHMEM_OFFSET + 0x0)
#define TASK_CB_SHMEM_START       (TASK_CB_SHMEM_SID + 0x4)
#define TASK_CB_SHMEM_END         (TASK_CB_SHMEM_START + 0x4)
#define TASK_CB_SHMEM_USE_LIST    (TASK_CB_SHMEM_END + 0x4)
#define TASK_CB_MAILADDR_OFFSET   (TASK_CB_SHMEM_USE_LIST + 0x8) 
#else
#define TASK_CB_MAILADDR_OFFSET    (TASK_CB_SHMEM_OFFSET + 0x0)
#endif

#ifdef LOSCFG_COMPAT_CMSIS_VER_1
#define TASK_CB_MAILADDR_START      (TASK_CB_MAILADDR_OFFSET + 0x0)
#define TASK_CB_MAILADDR_END        (TASK_CB_MAILADDR_START + 0x4)
#define TASK_CB_IPC_RECOVER_OFFSET  (TASK_CB_MAILADDR_END + 0x4)
#else
#define TASK_CB_IPC_RECOVER_OFFSET  (TASK_CB_MAILADDR_OFFSET + 0x0)
#endif

#ifdef LOSCFG_BASE_IPC_RECOVER
#define TASK_CB_IPC_RECOVER_ADDR    (TASK_CB_IPC_RECOVER_OFFSET + 0x0)
#define TASK_CB_TASK_ENTRY          (TASK_CB_IPC_RECOVER_ADDR + 0x4)
#else
#define TASK_CB_TASK_ENTRY          (TASK_CB_IPC_RECOVER_OFFSET + 0x0)
#endif

#ifdef LOSCFG_BASE_IPC_SEM
#define TASK_CB_TASK_SEM            (TASK_CB_TASK_ENTRY + 0x4)
#define TASK_CB_MUX_OFFSET          (TASK_CB_TASK_SEM + 0x4)
#else
#define TASK_CB_MUX_OFFSET          (TASK_CB_TASK_ENTRY + 0x0)
#endif

#ifdef LOSCFG_BASE_IPC_MUX
#define TASK_CB_TASK_MUX            (TASK_CB_MUX_OFFSET + 0x0)
#define TASK_CB_JOINABLE_OFFSET     (TASK_CB_TASK_MUX + 0x4)
#else
#define TASK_CB_JOINABLE_OFFSET     (TASK_CB_MUX_OFFSET + 0x0)
#endif

#ifdef LOSCFG_TASK_JOINABLE
#define TASK_CB_JOINNER             (TASK_CB_JOINABLE_OFFSET + 0x0)
#define TASK_CB_JOINED              (TASK_CB_JOINNER + 0x4)
#define TASK_CB_TASK_RET_VAL        (TASK_CB_JOINED + 0x4)
#define TASK_CB_ARGS                (TASK_CB_TASK_RET_VAL + 0x4)
#else
#define TASK_CB_ARGS                (TASK_CB_JOINABLE_OFFSET + 0x0) 
#endif

#ifdef LOSCFG_OBSOLETE_API
#define TASK_CB_TASK_NAME           (TASK_CB_ARGS + 4 * 4)
#else
#define TASK_CB_TASK_NAME           (TASK_CB_ARGS + 0x4)
#endif

#define TASK_CB_PENDLIST            (TASK_CB_TASK_NAME + 0x4)
#define TASK_CB_SORTLIST            (TASK_CB_PENDLIST + 0x8) /* 0x8:LOS_DL_LIST */

#ifdef LOSCFG_BASE_IPC_EVENT
#define TASK_CB_EVENT               (TASK_CB_SORTLIST + 0xC) /* 0xC : SortLinkList */
#define TASK_CB_EVENT_MASK          (TASK_CB_EVENT + 0xC) /* 0xC : EVENT_CB_S */
#define TASK_CB_EVENT_MODE          (TASK_CB_EVENT_MASK + 0x4)
#define TASK_CB_EVENT_MSG           (TASK_CB_EVENT_MODE + 0x4)
#else
#define TASK_CB_EVENT_MSG           (TASK_CB_SORTLIST + 0xC)
#endif

#define TASK_CB_PRI_BITMAP           (TASK_CB_EVENT_MSG + 0x4)

#ifdef LOSCFG_BASE_CORE_TIMESLICE
#define TASK_CB_TIME_SLICE          (TASK_CB_PRI_BITMAP + 0x4)
#define TASK_CB_PERF_OFFSET         (TASK_CB_TIME_SLICE + 0x4)
#else
#define TASK_CB_PERF_OFFSET         (TASK_CB_PRI_BITMAP + 0x4)
#endif

#ifdef LOSCFG_KERNEL_PERF
#define TASK_CB_PC                  (TASK_CB_PERF_OFFSET + 0x0)
#define TASK_CB_FP                  (TASK_CB_PERF_OFFSET + 0x4)

#ifdef LOSCFG_KERNEL_PERF_PER_TASK
#define TASK_CB_PERFEVENT          (TASK_CB_FP + 0x4)
#ifdef LOSCFG_ARCH_RISCV32
#define TASK_CB_BOXCALL_OFFSET     (TASK_CB_PERFEVENT + 0x120) 
#else
#define TASK_CB_BOXCALL_OFFSET     (TASK_CB_PERFEVENT + 0x112)
#endif
#else
#define TASK_CB_BOXCALL_OFFSET     (TASK_CB_FP + 0x4)
#endif
#else
#define TASK_CB_BOXCALL_OFFSET     (TASK_CB_PERF_OFFSET + 0x0)
#endif

#ifdef LOSCFG_BASE_IPC_KERNEL_BOXCALL
#define TASK_CB_BOXCALL_FLAG       (TASK_CB_BOXCALL_OFFSET + 0x0)
#define TASK_CB_BOXCALL_STACKID    (TASK_CB_BOXCALL_OFFSET + 0x4)
#define TASK_CB_SYSCALL_SAVESP     (TASK_CB_BOXCALL_STACKID + 0x4)
#define TASK_CB_LOCK_DEP_OFFSET    (TASK_CB_SYSCALL_SAVESP + 0x8)
#else
#define TASK_CB_LOCK_DEP_OFFSET    (TASK_CB_BOXCALL_OFFSET + 0x0)
#endif

#ifdef LOSCFG_KERNEL_LOCKDEP
#define TASK_CB_LOCK_DEP           (TASK_CB_LOCK_DEP_OFFSET + 0x0)
#define TASK_CB_CPUP_OFFSET        (TASK_CB_LOCK_DEP_OFFSET + 0x4 + \
                                    0x184 * (LOSCFG_KERNEL_MUXDEP + LOSCFG_KERNEL_SEMDE + LOSCFG_PTHREAD_MUXDEP))
#else
#define TASK_CB_CPUP_OFFSET        (TASK_CB_LOCK_DEP_OFFSET + 0x0)
#endif

#ifdef LOSCFG_KERNEL_CPUP
#define TASK_CB_CPUP                   (TASK_CB_CPUP_OFFSET + 0x0) 
#define TASK_CB_TRACE_EVT_CNT_OFFSET   (TASK_CB_CPUP_OFFSET + 0x14 + 0x4 * (LOSCFG_CPUP_HISTORY_RECORD_NUM + 1)) 
#else
#define TASK_CB_TRACE_EVT_CNT_OFFSET   (TASK_CB_CPUP_OFFSET + 0x0)
#endif

#if defined(LOSCFG_TRACE_OBJ_MAX_NUM) && LOSCFG_TRACE_OBJ_MAX_NUM > 0
#define TASK_CB_TRACE_EVT_CNT          (TASK_CB_TRACE_EVT_CNT_OFFSET + 0x0)
#define TASK_CB_DEBUG_TASK_OFFSET      (TASK_CB_TRACE_EVT_CNT_OFFSET + 0x4)
#else
#define TASK_CB_DEBUG_TASK_OFFSET      (TASK_CB_TRACE_EVT_CNT_OFFSET + 0x0)
#endif

#ifdef LOSCFG_DEBUG_TASK
#define TASK_CB_CUR_STATUS_TIME        (TASK_CB_DEBUG_TASK_OFFSET + 0x0)
#define TASK_CB_SYSCALL_CNT_OFFSET     (TASK_CB_DEBUG_TASK_OFFSET + 0x8)
#else
#define TASK_CB_SYSCALL_CNT_OFFSET     (TASK_CB_DEBUG_TASK_OFFSET + 0x0)
#endif

#define TASK_CB_SYSCALL_NEST_CNT       (TASK_CB_SYSCALL_CNT_OFFSET + 0x0)

/* boxCB offset */
#define MEM_LAYOUT_TEXT_START               0
#define MEM_LAYOUT_TEXT_END                 (MEM_LAYOUT_TEXT_START + 0x4)
#define MEM_LAYOUT_RODATA_START             (MEM_LAYOUT_TEXT_END + 0x4)
#define MEM_LAYOUT_RODATA_END               (MEM_LAYOUT_RODATA_START + 0x4)
#define MEM_LAYOUT_DATA_START               (MEM_LAYOUT_RODATA_END + 0x4)
#define MEM_LAYOUT_DATA_END                 (MEM_LAYOUT_DATA_START + 0x4)
#define MEM_LAYOUT_POOL_ADDR                (MEM_LAYOUT_DATA_END + 0x4)
#define MEM_LAYOUT_POOL_END_ADDR            (MEM_LAYOUT_POOL_ADDR + 0x4)
#define MEM_LAYOUT_POOL_SIZE                (MEM_LAYOUT_POOL_END_ADDR + 0x4)
#define MEM_LAYOUT_DATA_LOAD_START          (MEM_LAYOUT_POOL_SIZE + 0x4)
#define MEM_LAYOUT_DATA_LOAD_END            (MEM_LAYOUT_DATA_LOAD_START + 0x4)
#define MEM_LAYOUT_DATA_LOAD_SIZE           (MEM_LAYOUT_DATA_LOAD_END + 0x4)
#define MEM_LAYOUT_BSS_START                (MEM_LAYOUT_DATA_LOAD_SIZE + 0x4)
#define MEM_LAYOUT_BSS_END                  (MEM_LAYOUT_BSS_START + 0x4)
#define MEM_LAYOUT_BSS_SIZE                 (MEM_LAYOUT_BSS_END + 0x4)
#ifdef LOSCFG_BOX_KERNEL_MEM            
#define MEM_LAYOUT_KERNEL_POOL_ADDR         (MEM_LAYOUT_BSS_SIZE + 0x4)
#define MEM_LAYOUT_KERNEL_POOL_END_ADDR     (MEM_LAYOUT_KERNEL_POOL_ADDR + 0x4)
#define BOX_CB_MEM_LAYOUT_SIZE              (MEM_LAYOUT_KERNEL_POOL_END_ADDR + 0x4)
#else
#define BOX_CB_MEM_LAYOUT_SIZE              (MEM_LAYOUT_BSS_SIZE + 0x4)
#endif

#define BOX_CB_TASK_NAMELEN       (32)
#define BOX_CB_TASK_PRI_SIZE      (2)
#define BOX_CB_SIZE_ALIGN_MASK    (0x3)

#define BOX_CB_MEM_LAYOUT_BASE    0x4
#define BOX_CB_TEXT_START(idx)    (BOX_CB_MEM_LAYOUT_BASE + (idx) * BOX_CB_MEM_LAYOUT_SIZE)
#define BOX_CB_TEXT_END(idx)      (BOX_CB_MEM_LAYOUT_BASE + 0x4 + (idx) * BOX_CB_MEM_LAYOUT_SIZE)
#define BOX_CB_RODATA_START(idx)  (BOX_CB_MEM_LAYOUT_BASE + 0x8 + (idx) * BOX_CB_MEM_LAYOUT_SIZE)
#define BOX_CB_RODATA_END(idx)    (BOX_CB_MEM_LAYOUT_BASE + 0xc + (idx) * BOX_CB_MEM_LAYOUT_SIZE)
#define BOX_CB_DATA_START(idx)    (BOX_CB_MEM_LAYOUT_BASE + 0x10 + (idx) * BOX_CB_MEM_LAYOUT_SIZE)
#define BOX_CB_DATA_END(idx)      (BOX_CB_MEM_LAYOUT_BASE + 0x14 + (idx) * BOX_CB_MEM_LAYOUT_SIZE)
#define BOX_CB_HEAP_START(idx)    (BOX_CB_MEM_LAYOUT_BASE + 0x18 +(idx) * BOX_CB_MEM_LAYOUT_SIZE)
#define BOX_CB_TASK_CB_LIST(lNum)     ((BOX_CB_MEM_LAYOUT_SIZE * (lNum)) + BOX_CB_MEM_LAYOUT_BASE)
#define BOX_CB_TSK_ENTRY_FUNC(lNum)   (BOX_CB_TASK_CB_LIST(lNum) + 0x8)
#define BOX_CB_SWTMR_TASK_ID(lNum)    (BOX_CB_TSK_ENTRY_FUNC(lNum) + 0x8)
#ifdef LOSCFG_BASE_CORE_SWTMR
#define BOX_CB_SWTMR_EXPIRY_LIST(lNum)  (BOX_CB_SWTMR_TASK_ID(lNum) + 0x4)
#define BOX_CB_BOX_PERM(lNum)           (BOX_CB_SWTMR_EXPIRY_LIST(lNum) + 0xC)
#else
#define BOX_CB_BOX_PERM(lNum)           (BOX_CB_SWTMR_TASK_ID(lNum) + 0x4)
#endif
#ifdef LOSCFG_BASE_SID_LIST
#define BOX_CB_SID_USED_LIST(lNum)      (BOX_CB_BOX_PERM(lNum) + 0x4)
#define BOX_CB_SID_FREE_LIST(lNum)      (BOX_CB_SID_USED_LIST(lNum) + 0x8)
#define BOX_CB_BOX_SID_POOL(lNum)       (BOX_CB_SID_FREE_LIST(lNum) + 0x8)
#else
#define BOX_CB_BOX_SID_POOL(lNum)       (BOX_CB_BOX_PERM(lNum) + 0x4)
#endif
#define BOX_CB_BOX_SID_POOL_SIZE(lNum)  (BOX_CB_BOX_SID_POOL(lNum) + 0x4)
#define BOX_CB_STACK(lNum)              (BOX_CB_BOX_SID_POOL_SIZE(lNum) + 0x8)
#define BOX_CB_ENTRY_TASK_NAME(lNum)    (BOX_CB_STACK(lNum) + 0x4)
#define BOX_CB_DYNBOX_WORD(lNum)        (BOX_CB_ENTRY_TASK_NAME(lNum) + \
                                         ((BOX_CB_TASK_NAMELEN + BOX_CB_TASK_PRI_SIZE + BOX_CB_SIZE_ALIGN_MASK) & \
                                         (~BOX_CB_SIZE_ALIGN_MASK)))

#define BOX_CB_SHELL_PERM(lNum)         (BOX_CB_DYNBOX_WORD(lNum) + 0x4)
#ifdef LOSCFG_SHELL
#define BOX_CB_SHELL_HANDLE(lNum)       (BOX_CB_SHELL_PERM(lNum) + 0x4)
#define BOX_CB_ENABLE_LMS(lNum)         (BOX_CB_SHELL_HANDLE(lNum) + 0x4)
#else
#define BOX_CB_ENABLE_LMS(lNum)         (BOX_CB_SHELL_PERM(lNum) + 0x4)
#endif

#ifndef LOSCFG_TASK_STACK_REUSE
#define BOX_CB_RECYCLE_STACK_LIST(lNum) (BOX_CB_ENABLE_LMS(lNum) + 0x4)
#define BOX_CB_RECYCLE_STACK_NUM(lNum)  (BOX_CB_RECYCLE_STACK_LIST(lNum) + 0x4)
#define BOX_CB_RES_START_ADDR(lNum)     (BOX_CB_RECYCLE_STACK_NUM(lNum) + 0x4)
#else
#define BOX_CB_RES_START_ADDR(lNum)     (BOX_CB_ENABLE_LMS(lNum) + 0x4)
#endif
#define BOX_CB_RES_END_ADDR(lNum)       (BOX_CB_RES_START_ADDR(lNum) + 0x4)

#ifdef LOSCFG_BASE_IPC_CONDVAR
#define BOX_CB_RES_COND_LIST(lNum)      (BOX_CB_RES_END_ADDR(lNum) + 28)
#define BOX_CB_RES_MUX_LIST(lNum)       (BOX_CB_RES_COND_LIST(lNum) + 0xC)
#else
#define BOX_CB_RES_MUX_LIST(lNum)       (BOX_CB_RES_END_ADDR(lNum) + 28)
#endif

#define BOX_CB_RES_SWT_LIST(lNum)       (BOX_CB_RES_MUX_LIST(lNum) + 48)
#define BOX_CB_RES_SWT_SIZE        (0xC)
#ifdef LOSCFG_KERNEL_MUXDEP
#define BOX_CB_MUXDEP_INFO_SIZE    (0x4)
#else
#define BOX_CB_MUXDEP_INFO_SIZE    (0)
#endif

#ifdef LOSCFG_KERNEL_SEMDEP
#define BOX_CB_SEMDEP_INFO_SIZE    (0x4)
#else
#define BOX_CB_SEMDEP_INFO_SIZE    (0)
#endif

#ifdef LOSCFG_COMPAT_POSIX_MQUEUE
#define BOX_CB_POSIX_MQUEUE_SIZE   (16)
#else
#define BOX_CB_POSIX_MQUEUE_SIZE   (0)
#endif
#define LOS_BOX_CB_SIZE(lNum) (BOX_CB_RES_SWT_LIST((lNum)) + BOX_CB_RES_SWT_SIZE + BOX_CB_MUXDEP_INFO_SIZE + \
                                BOX_CB_SEMDEP_INFO_SIZE + BOX_CB_POSIX_MQUEUE_SIZE) 

#define SYS_MEM_LAYOUT_SIZE           0x18
#define SYS_MEM_LAYOUT_TEXT_START(idx)    (0x0 + (idx) * SYS_MEM_LAYOUT_SIZE)
#define SYS_MEM_LAYOUT_TEXT_END(idx)      (0x4 + (idx) * SYS_MEM_LAYOUT_SIZE)  
#define SYS_MEM_LAYOUT_RODATA_START(idx)  (0x8 + (idx) * SYS_MEM_LAYOUT_SIZE) 
#define SYS_MEM_LAYOUT_RODATA_END(idx)    (0xc + (idx) * SYS_MEM_LAYOUT_SIZE)
#define SYS_MEM_LAYOUT_DATA_START(idx)    (0x10 + (idx) * SYS_MEM_LAYOUT_SIZE)
#define SYS_MEM_LAYOUT_DATA_END(idx)      (0x14 + (idx) * SYS_MEM_LAYOUT_SIZE)

/* task status */
#define OS_TASK_STATUS_RUNNING    0x0010
#define OS_TASK_STATUS_INUSER     0x200
#endif
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ARCH_TASK_BOX_CB_H */