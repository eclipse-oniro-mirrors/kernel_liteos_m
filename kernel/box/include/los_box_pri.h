/*
 * los_box_pri.h — BOX feature private header: box control block,
 * memory-layout structs and the box core prototypes.
 *
 *  - the target-layer constants this feature owns are defined here behind
 *    #ifndef, so a board config can still override them
 *    (LOSCFG_BASE_BOX_MAX_LIMIT / OS_BOX_MEM_LAYOUT_NUM / LOS_BOX_BASE_ID /
 *    LOSCFG_USR_THREAD_KERNEL_STACK_SIZE);
 *  - LosBoxCB keeps every field that boxes_auto.cfg initializes, with the
 *    same name and order; the config-gated fields that are OFF in this build
 *    (SHELL handle, TASK_STACK_REUSE recycle lists, MQUEUE, VFS_PERM path,
 *    MUXDEP/SEMDEP, dynload) are left out;
 *  - ExpiryList comes from los_percpu_pri.h.
 */
#ifndef LOS_BOX_PRI_H
#define LOS_BOX_PRI_H

#include "los_typedef.h"
#include "los_list.h"
#include "los_task.h"
#include "los_task_base.h"    /* LosTaskCB for the task-pool prototypes */
#include "los_error.h"        /* LOS_ERRNO_OS_ERROR for the box errno block */
#include "los_sec_pri.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ---- target-layer constants (defined here behind #ifndef) ---- */
#ifndef LOSCFG_BASE_BOX_MAX_LIMIT
#define LOSCFG_BASE_BOX_MAX_LIMIT       2   /* boxA + boxB */
#endif
#ifndef OS_BOX_MEM_LAYOUT_NUM
#define OS_BOX_MEM_LAYOUT_NUM           1
#endif
#ifndef LOS_BOX_BASE_ID
#define LOS_BOX_BASE_ID                 0   /* box ids are array indices:
                                            * g_boxCBArr[boxId] == the box CB.
                                            * Encoded ids stay kernel-distinct:
                                            * OS_BOX_SET_ID(0, 0) = 0x10000. */
#endif
/* boxRes window: both boxes share the R2 region of the memory map, and
 * boxes_auto.cfg wires the real span, so the per-box res-region check is an
 * R2 containment check. */

#define OS_BOX_SERVICE_LIMIT            10

/* ---- box errnos: routed through the LOS_ERRNO_OS_ERROR machinery with a
 * module id of 0x2A, which sits outside the module enum range in los_error.h,
 * so it is a local define and that header stays untouched. ---- */
#define LOS_MOD_BOX                     0x2A
#define LOS_ERRNO_BOX_BOXID_ERR         LOS_ERRNO_OS_ERROR(LOS_MOD_BOX, 0x00)
#define LOS_ERRNO_BOX_STATUS_ERR        LOS_ERRNO_OS_ERROR(LOS_MOD_BOX, 0x01)
#define LOS_ERRNO_BOX_TSK_INIT_ERR      LOS_ERRNO_OS_ERROR(LOS_MOD_BOX, 0x03)
#define LOS_ERRNO_BOX_INVALID_ADDR      LOS_ERRNO_OS_ERROR(LOS_MOD_BOX, 0x04)
#define LOS_ERRNO_BOX_MEMCPY_ERR        LOS_ERRNO_OS_ERROR(LOS_MOD_BOX, 0x05)

/* ---- box status ---- */
#define OS_BOX_UNINIT                   ((UINT8)0U)
#define OS_BOX_INIT                     ((UINT8)1U)
#define OS_BOX_INITING                  ((UINT8)2U)
#define OS_BOX_SET_TIME                 ((UINT8)0x10)
#define OS_BOX_TRUST                    ((UINT8)0x20)
#define OS_BOX_NOLOAD                   ((UINT8)0x80)

#define BOX_SHPERM_DENY                 0U
#define OS_BOX_SHPERM_DENY              0U   /* same value as BOX_SHPERM_DENY; this is
                                             * the spelling boxes_auto.cfg uses */
#define OS_BOX_SHPERM_ALLOW             1U
#define OS_KERNEL_BOX                   0xFFU

#define OS_BOX_INIT_STATUS_MASK         ((UINT8)0x3)
#define BOX_SET_TIME_RIGHT(boxStatus)   (((boxStatus) & OS_BOX_SET_TIME) == OS_BOX_SET_TIME)
#define BOX_IS_NOLOAD(boxStatus)        (((boxStatus) & OS_BOX_NOLOAD) == OS_BOX_NOLOAD)
#define BOX_IS_INIT(boxStatus)          (((boxStatus) & OS_BOX_INIT_STATUS_MASK) == OS_BOX_INIT)
#define BOX_IS_INITING(boxStatus)       (((boxStatus) & OS_BOX_INIT_STATUS_MASK) == OS_BOX_INITING)
#define BOX_IS_UNINIT(boxStatus)        (((boxStatus) & OS_BOX_INIT_STATUS_MASK) == OS_BOX_UNINIT)
#define BOX_IS_TRUST(boxStatus)         (((boxStatus) & OS_BOX_TRUST) == OS_BOX_TRUST)
#define BOX_SET_INIT_STATUS(boxStatus, initStatus) \
    ((boxStatus) = ((boxStatus) & ~OS_BOX_INIT_STATUS_MASK) | (initStatus))

#define OS_BOX_GET_BID(id)              ((((id) >> 16) & (UINT32)0xFFFF) - (UINT32)1)
#define OS_BOX_GET_ID(id)               ((id) & (UINT32)0xFFFF)
#define OS_BOX_SET_ID(boxId, id)        (((UINT32)((boxId) + (UINT32)1) << 16) | (id & (UINT32)0xFFFF))

/* ---- R2 user-readable section placements ----
 * USER_READONLY_RAM is the window the MPU maps kernel-RW / user-RO. Kernel
 * variables the box usr world must read live here: g_sysMode /
 * g_curBoxId / g_boxLocalPool / g_usrTickCount / the stack canary. Not to be
 * confused with the usr ro-data section - that one is flash (COMMON_FLASH),
 * while these variables are written at RUN time (the dispatch asm updates
 * g_sysMode on every kernel<->user switch). Section rules in liteos.ld,
 * region bounds in asm/memmap_config.h.
 * NOTE: los_builddef_target.h is NOT an option for these - kernel modules
 * never see that header (only bsp_config and box_usr_demo force-include it),
 * so this header is the shared home for box kernel TUs. */
#ifndef LITE_OS_SEC_USER_RO_DATA
#define LITE_OS_SEC_USER_RO_DATA        __attribute__((section(".USER_READONLY_DATA")))
#endif

#ifndef LITE_OS_SEC_USER_RO_BSS
#define LITE_OS_SEC_USER_RO_BSS         __attribute__((section(".USER_READONLY_BSS")))
#endif

/* .usrtcm.text: the COMMON_TEXT slot for code shared with and callable
 * from the USER world (the refs "usr ROM" section). Kernel TUs cannot
 * spell LITE_OS_SEC_TEXT_USRROM here: the generic los_builddef.h does not
 * define it at all, and kernel modules never force-include
 * los_builddef_target.h ("kernel modules stay default") - the same reason
 * the USER_RO_* macros above live in this header. */
#define LITE_OS_SEC_USRROM_TEXT     __attribute__((section(".usrtcm.text")))

/* .usr.text: the COMMON_TEXT slot for the user-world API wrappers
 * (los_usr_api.c) - user-executable AND kernel-callable, collected by the
 * lds rule for .usr.text. Lives here for the same reason as the two above:
 * its only consumer is now a kernel/box TU, which never sees the target
 * builddef header. */
#ifndef LITE_OS_SEC_TEXT_USRSPACE
#define LITE_OS_SEC_TEXT_USRSPACE   __attribute__((section(".usr.text")))
#endif

/* Kernel-mode value of g_sysMode (refs home: arch_s include/arch/cpu.h). */
#define SYS_MODE_DEFAULT_KERNEL     0

/* ---- trust-context / SID plumbing helpers ----
 *   - IS_USER_ADDR() is FALSE, so IS_TRUST_CONTEXT() is TRUE and the
 *     check-valid family short-circuits to "allow non-NULL"; wiring the real
 *     userspace probe (IsParamAddrInUserspace) is what tightens it;
 *   - IS_DRV_BOX() is FALSE: this build has no driver box;
 *   - OS_SID_BOX_INIT() hands the box's own SID pool array and size to the
 *     SID module and returns its result. */
#ifndef KERNEL_BOX_LIMIT
#define KERNEL_BOX_LIMIT                LOSCFG_BASE_BOX_MAX_LIMIT
#endif
#define LOS_IN_KERNEL                   (g_sysMode == 0)
#define IS_USER_ADDR()                  0                   /* TODO: IsParamAddrInUserspace() */
#define IS_DRV_BOX(boxid)               0                   /* TODO: LOS_DRV_BOX_ID range check */
#define IS_TRUST_CONTEXT(boxCB)         (((boxCB) == NULL) || (IS_DRV_BOX((boxCB)->boxId)) || !IS_USER_ADDR())
/* non-driver-box form: no owner/free lists are passed in, the SID module
 * keeps the per-box free lists itself (see los_sec_pri.h). */
#define OS_SID_BOX_INIT(box)            OsSIdBoxInit(NULL, NULL, (box)->boxSIdPool, (box)->boxSIdPoolSize)

/* ---- layout types ---- */
typedef struct SysMemLayoutTag {
    union {
        struct {
            UINTPTR sysUsrTextStartAddr;
            UINTPTR sysUsrTextEndAddr;
            UINTPTR sysUsrRodataStartAddr;
            UINTPTR sysUsrRodataEndAddr;
            UINTPTR sysUsrDataStartAddr;
            UINTPTR sysUsrDataEndAddr;
        };
        struct {
            UINTPTR textStartAddr;
            UINTPTR textEndAddr;
            UINTPTR rodataStartAddr;
            UINTPTR rodataEndAddr;
            UINTPTR dataStartAddr;
            UINTPTR dataEndAddr;
        };
    };
} SysMemLayout;

typedef struct BoxMemLayoutTag {
    UINTPTR boxTextStartAddr;
    UINTPTR boxTextEndAddr;
    UINTPTR boxRodataStartAddr;
    UINTPTR boxRodataEndAddr;
    UINTPTR boxDataStartAddr;
    UINTPTR boxDataEndAddr;
    VOID *boxMemPoolAddr;
    VOID *boxMemPoolEndAddr;
    UINT32 boxMemPoolSize;
    UINTPTR boxDataLoadStartAddr;
    UINTPTR boxDataLoadEndAddr;
    UINT32 boxDataLoadSize;
    UINTPTR boxBssStartAddr;
    UINTPTR boxBssEndAddr;
    UINT32 boxBssSize;
} BoxMemLayout;

/* ExpiryList comes from los_percpu_pri.h */
#include "los_percpu_pri.h"

/* ---- control block: field names and order match what boxes_auto.cfg
 * initializes ---- */
typedef struct LosBoxCBtag {
    UINT16 boxId;
    BoxMemLayout layout[OS_BOX_MEM_LAYOUT_NUM];
    LOS_DL_LIST taskCBList;
    TSK_ENTRY_FUNC boxEntryTask;
    UINT8 boxStatus;
    UINT32 swtmrTaskId;
#ifdef LOSCFG_BASE_CORE_SWTMR
    ExpiryList swtmrExpiryList;
#endif
    UINT32 boxPerm;
    SIdObj *boxSIdPool;
    UINT32 boxSIdPoolSize;
    UINT32 entryTaskStackSize;
    UINT8 *stack;
    CHAR entryTaskName[LOS_TASK_NAMELEN];
    UINT16 entryTaskPrio;
    UINT32 dynboxWord;
    UINT32 shellPerm;
    UINT32 enableLms;
#ifndef LOSCFG_TASK_STACK_REUSE
    /* without LOSCFG_TASK_STACK_REUSE: deleted dynamic user stacks are queued
     * here for the box to free from its own pool (usr-side
     * LOS_TaskResRecycle). */
    VOID *boxResRecycleStackList;
    UINT32 boxResRecycleStackNum;
#endif
    UINTPTR boxResStartAddr;
    UINTPTR boxResEndAddr;
    VOID *boxResTaskList;
    VOID *boxResStackList;
    UINT32 boxResTaskNum;
    VOID *boxResEvtList;
    VOID *boxResEvtEntityList;
    UINT32 boxResEvtNum;
    /* Condvar pool, refs los_box_pri.h shape (there it is #ifdef'd on
     * LOSCFG_BASE_IPC_CONDVAR; kept unconditional here like the Shm trio
     * below so the struct layout does not move with config - the boxes cfg
     * simply leaves them NULL when the module is off). */
    VOID *boxResCondList;
    VOID *boxResCondEntityList;
    UINT32 boxResCondNum;
    VOID *boxResMuxList;
    VOID *boxResMuxEntityList;
    UINT32 boxResMuxNum;
    VOID *boxResQueList;
    VOID *boxResQueEntityList;
    UINT32 boxResQueNum;
    VOID *boxResSemList;
    VOID *boxResSemEntityList;
    UINT32 boxResSemNum;
    VOID *boxResShmList;
    VOID *boxResShmEntityList;
    UINT32 boxResShmNum;
    /* box-provided timer pool + deferred-callback ring, attached by the
     * swtmr resource init. Zero-size boxes leave both NULL/0 and the swtmr
     * resource init no-ops. */
    VOID *boxResSwtList;
    UINTPTR boxResSwtQueue;
    UINT32 boxResSwtNum;
    UINT32 boxResSwtQueueLen;
} LosBoxCB;

/* LosTaskStack comes from los_sec_pri.h (chain visibility) */
/* BOXCALL service table: OS_BOX_SERVICE_LIMIT service entries per box */
typedef struct {
    VOID *service[OS_BOX_SERVICE_LIMIT];
} BOX_SERVICE_TABLE;

/* ---- globals ---- */
extern LosBoxCB *g_boxCBArr;
extern LosBoxCB g_userBoxCBArr[LOSCFG_BASE_BOX_MAX_LIMIT];
extern SysMemLayout *g_sysMemLayout;
extern SysMemLayout g_sysMemLayoutArr[OS_BOX_MEM_LAYOUT_NUM];   /* boxes_conf.c */
extern BOX_SERVICE_TABLE g_boxServiceTable[LOSCFG_BASE_BOX_MAX_LIMIT];
/* per-box TCB free list (los_task_box.c; shared with the task recycle path) */
extern LOS_DL_LIST g_boxFreeTask[LOSCFG_BASE_BOX_MAX_LIMIT];
/* current-context globals: the kernel writes them and the user world reads
 * them directly, so they must be placed in a user-readable rodata section
 * (LITE_OS_SEC_RODATA_USRSPACE). */
extern UINT32 g_sysMode;                            /* 0 kernel / 1 user thread */
extern volatile UINT32 g_curBoxId;                  /* id of the running box */
extern UINTPTR g_boxLocalPool[OS_BOX_MEM_LAYOUT_NUM];

/* ---- entry points (kernel/box/los_box.c + arch glue) ---- */
extern UINT32 OsBoxInit(LosBoxCB * const userBoxArr, SysMemLayout * const sysMemLayoutArr);
extern LosBoxCB *OsGetCurBoxCB(VOID);
extern VOID MpuLayoutInit(VOID);
extern VOID BoxMpuSwitch(const LosBoxCB *boxCB);
/* arch/arm/cortex_m/box/los_box_task.c: craft the user context + patch the
 * kernel ctx PC to the OsUserTaskEntry prelude; returns the user stack
 * pointer. */
extern VOID *OsBoxTskUserStackInit(LosTaskCB *taskCB, VOID *userStackBase, UINT32 userStackSize);
/* syscall entry (arch/arm/cortex_m/box/los_svc_entry.S) */
extern VOID OsExcSvcCall(VOID);

/* ---- task-module box mechanisms (los_task_box.c) ---- */
/* per-box TCB + kernel-stack pools and the create family. */
extern UINT32 OsBoxTaskInit(UINT16 boxId, LosTaskCB *taskList, LosTaskStack *stackList, UINT32 taskNum);
/* exported for the box swtmr task bring-up (a real box task: box pool TCB,
 * pool kernel stack, boxCB/curBoxId/INUSER binding). usrTopStack is the
 * caller-provided user stack base (box heap for runtime tasks). */
#define STACK_TYPE_STATIC    0
#define STACK_TYPE_DYNAMIC   1
extern UINT32 OsUsrTaskCreateOnly(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *usrTopStack,
                                  LosBoxCB *boxCB, BOOL taskStackType);
extern BOOL OsBoxTaskIdCheckInvalid(UINT32 taskId);
extern UINT32 OsBoxTaskGetFreeTaskCB(LosTaskCB **taskCB, const LosBoxCB *boxCB);
extern VOID OsBoxTaskFreeUsedTaskCB(LosTaskCB *taskCB, const LosBoxCB *boxCB);
extern VOID *OsBoxKernelStackAlloc(const LosBoxCB *boxCB, UINT32 stackSize);
extern VOID OsBoxKernelStackFree(LosTaskCB *taskCB);
extern VOID OsBoxKernelStackFreeNode(VOID *stackBase, const LosBoxCB *boxCB);
extern VOID OsBoxUserStackRecycle(LosTaskCB *taskCB);
extern UINT32 OsFirstBoxTaskCreate(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack,
                                   LosBoxCB *boxCB);

/* ---- task dynamic create + user-stack recycle (los_task_box.c) ----
 * OsTaskCreateOnlyDynamic (syscall 38) and OsTaskGetRecycleStack (syscall 39)
 * are reached from user mode through the syscall table. The usr-side wrappers
 * (LOS_TaskCreateOnly / LOS_TaskCreate / LOS_TaskStackRecycle) live in
 * los_usr_api.c in this directory, built by the box_usr module. */
extern UINT32 OsTaskCreateOnlyDynamic(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *usrTopStack);
extern UINT32 OsTaskKernelCreateDynamicDo(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack);
extern VOID  *OsTaskGetRecycleStack(VOID);

/* ---- per-box resource pool init + lifecycle (los_box.c) ----
 * OsBoxResInit attaches each boxRes* pool descriptor in the box CB to its
 * module (task and SID first, then the IPC pools, then swtmr). */
extern UINT32 OsBoxResInit(LosBoxCB *box);

/* ---- box lifecycle (los_box.c + los_task_box.c) ---- */
extern UINT32 OsGetBoxAndSetStatus(UINT16 boxId, LosBoxCB **boxCB);
extern UINT32 OsBoxRestart(UINT16 boxId);
extern VOID OsBoxDestroy(UINT16 boxId);
extern VOID OsTaskDestroy(LosBoxCB *boxCB);           /* los_task_box.c */

/* SID / swtmr destroy helpers, driven by the box destroy sequence.
 * OsSIdBoxInit and the rest of the SID module contracts live in
 * los_sec_pri.h (implemented in los_sec.c). */
extern VOID OsSIdDrvBoxIpcRelease(const LosBoxCB *boxCB);
extern VOID OsSIdBoxResDestroy(const LosBoxCB *boxCB);
extern VOID OsSwtmrDestroy(const LosBoxCB *boxCB);

/* ---- check-valid + user-copy layer (los_box.c) ----
 * The WithBox/WithSection helpers test an address window against the box
 * layout sections (text / rodata / data / pool). The top-level
 * OsCheckValid*Addr / OsCopy* entry points short-circuit to "non-NULL is
 * valid" in interrupt and exception context and in a trust context; because
 * IS_USER_ADDR() is stubbed off, every context is currently a trust context,
 * so wiring the real userspace probe is what makes the section checks bite. */
extern UINT32 OsCheckValidAddrWithSection(UINTPTR address, UINT32 len, const LosBoxCB *boxCB);
extern UINT32 OsCheckValidAddrWithBox(UINTPTR address, UINT32 len);
extern UINT32 OsCheckValidTextAddr(UINTPTR address, UINT32 len);
extern UINT32 OsCheckValidRodataAddr(UINTPTR address, UINT32 len);
extern UINT32 OsCheckValidPoolAddr(UINTPTR address, UINT32 len);
extern UINT32 OsCheckValidDataAddr(UINTPTR address, UINT32 len);
extern UINT32 OsCheckValidAddr(UINTPTR address, UINT32 len);
extern UINT32 OsCopyFromUser(VOID *to, const VOID *from, UINT32 size);
extern UINT32 OsCopyToUser(VOID *to, const VOID *from, UINT32 size);
extern UINT32 OsStrlenFromUser(const CHAR *src, UINT32 count);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LOS_BOX_PRI_H */
