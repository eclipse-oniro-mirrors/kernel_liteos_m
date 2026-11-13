/*
 * los_box_pri.h — BOX feature private header, adapted port from
 * refs/liteos/kernel_s/base/include/los_box_pri.h.
 * M5A: relocated to kernel/base/include (mirrors the refs kernel_s/base/
 * include/ layout; was targets/.../box/kernel/ in the phase-1 demo layer).
 *
 * Adaptations for the OHOS liteos_m base (M2, faithful-first):
 *  - includes resolved to OHOS headers + local shims (los_sec_pri.h);
 *  - target-layer constants missing from refs are defined here
 *    (LOSCFG_BASE_BOX_MAX_LIMIT / OS_BOX_MEM_LAYOUT_NUM / LOS_BOX_BASE_ID /
 *    LOSCFG_USR_THREAD_KERNEL_STACK_SIZE);
 *  - LosBoxCB trimmed of config-gated fields that are OFF in this build
 *    (SHELL handle, TASK_STACK_REUSE recycle lists, MQUEUE, VFS_PERM path,
 *    MUXDEP/SEMDEP, dynload); every field initialized by boxes_auto.cfg
 *    is kept with the same name and order;
 *  - ExpiryList defined locally (refs keeps it in los_percpu_pri.h);
 *  - BOX_SERVICE_TABLE given a placeholder shape (real definition missing
 *    from refs; M5 BOXCALL restores it);
 *  - BOXCALL / KERNEL_BOXCALL / dynload / monitor blocks dropped (M5).
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

/* ---- target-layer constants (missing from refs, defined here) ---- */
#ifndef LOSCFG_BASE_BOX_MAX_LIMIT
#define LOSCFG_BASE_BOX_MAX_LIMIT       2   /* boxA + boxB */
#endif
#ifndef OS_BOX_MEM_LAYOUT_NUM
#define OS_BOX_MEM_LAYOUT_NUM           1
#endif
#ifndef LOS_BOX_BASE_ID
#define LOS_BOX_BASE_ID                 0   /* box ids are array indices (refs):
                                            * g_boxCBArr[boxId] == the box CB.
                                            * Encoded ids stay kernel-distinct:
                                            * OS_BOX_SET_ID(0, 0) = 0x10000. */
#endif
/* boxRes window: both boxes share the R2 region (M1 memory map); boxes_auto.cfg
 * wires the real span - the refs per-box res-region check becomes an R2
 * containment check here (M5B, placeholder retired). */

#define OS_BOX_SERVICE_LIMIT            10

/* ---- box errnos (refs kernel_s/include/los_box.h block, routed through
 * the OHOS errno machinery; module id 0x2A sits outside the OHOS enum range
 * as a local define so los_error.h stays untouched). refs spells the addr
 * one INVILID; both spellings resolve to the same value family. ---- */
#define LOS_MOD_BOX                     0x2A
#define LOS_ERRNO_BOX_BOXID_ERR         LOS_ERRNO_OS_ERROR(LOS_MOD_BOX, 0x00)
#define LOS_ERRNO_BOX_STATUS_ERR        LOS_ERRNO_OS_ERROR(LOS_MOD_BOX, 0x01)
#define LOS_ERRNO_BOX_TSK_INIT_ERR      LOS_ERRNO_OS_ERROR(LOS_MOD_BOX, 0x03)
#define LOS_ERRNO_BOX_INVALID_ADDR      LOS_ERRNO_OS_ERROR(LOS_MOD_BOX, 0x04)
#define LOS_ERRNO_BOX_MEMCPY_ERR        LOS_ERRNO_OS_ERROR(LOS_MOD_BOX, 0x05)

/* ---- box status (verbatim from refs) ---- */
#define OS_BOX_UNINIT                   ((UINT8)0U)
#define OS_BOX_INIT                     ((UINT8)1U)
#define OS_BOX_INITING                  ((UINT8)2U)
#define OS_BOX_SET_TIME                 ((UINT8)0x10)
#define OS_BOX_TRUST                    ((UINT8)0x20)
#define OS_BOX_NOLOAD                   ((UINT8)0x80)

#define BOX_SHPERM_DENY                 0U
#define OS_BOX_SHPERM_DENY              0U   /* refs inconsistency: cfg uses this name */
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

/* ---- scaffold helper macros (M5D: trust-context / SID plumbing) ----
 * refs los_task_pri.h / los_box_pri.h. The real IsParamAddrInUserspace /
 * SID module are not ported yet, so these resolve to safe scaffold values:
 *   - IS_USER_ADDR() always FALSE -> IS_TRUST_CONTEXT() always TRUE, so the
 *     check-valid family short-circuits to "allow non-NULL" (M5D wires the
 *     real userspace probe);
 *   - IS_DRV_BOX() always FALSE (no driver box in this build);
 *   - OS_SID_BOX_INIT() returns LOS_OK (SID pool init lands with the SID
 *     module). */
#ifndef KERNEL_BOX_LIMIT
#define KERNEL_BOX_LIMIT                LOSCFG_BASE_BOX_MAX_LIMIT
#endif
#define LOS_IN_KERNEL                   (g_sysMode == 0)   /* refs los_base.h */
#define IS_USER_ADDR()                  0                   /* TODO(M5D): IsParamAddrInUserspace() */
#define IS_DRV_BOX(boxid)               0                   /* TODO(M5+): LOS_DRV_BOX_ID range check */
#define IS_TRUST_CONTEXT(boxCB)         (((boxCB) == NULL) || (IS_DRV_BOX((boxCB)->boxId)) || !IS_USER_ADDR())
/* refs los_box_pri.h:238-240 (non-DRVBOX form). OsSIdBoxInit body is M5+. */
#define OS_SID_BOX_INIT(box)            OsSIdBoxInit(NULL, NULL, (box)->boxSIdPool, (box)->boxSIdPoolSize)

/* ---- layout types (verbatim from refs) ---- */
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

/* ExpiryList comes from the OHOS header (refs keeps it there too) */
#include "los_percpu_pri.h"

/* ---- control block (fields initialized by boxes_auto.cfg kept verbatim) ---- */
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
    /* refs (non-STACK_REUSE): deleted dynamic user stacks are queued here
     * for the box to free from its own pool (usr-side LOS_TaskResRecycle). */
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
    VOID *boxResSwtList;
    VOID *boxResSwtHandle;
    UINT32 boxResSwtNum;
} LosBoxCB;

/* LosTaskStack comes from the los_sec_pri.h shim (chain visibility) */
/* BOXCALL service table placeholder (real shape missing from refs, M5) */
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
/* current-context globals: kernel writes, user world reads directly
 * (refs los_usr_init.c keeps them in USRSPACE rodata for exactly this) */
extern UINT32 g_sysMode;                            /* 0 kernel / 1 user thread */
extern volatile UINT32 g_curBoxId;                  /* id of the running box */
extern UINTPTR g_boxLocalPool[OS_BOX_MEM_LAYOUT_NUM];

/* ---- entry points (kernel/base/los_box.c + arch glue) ---- */
extern UINT32 OsBoxInit(LosBoxCB * const userBoxArr, SysMemLayout * const sysMemLayoutArr);
extern LosBoxCB *OsGetCurBoxCB(VOID);
extern VOID MpuLayoutInit(VOID);
extern VOID BoxMpuSwitch(const LosBoxCB *boxCB);
/* arch/arm/cortex_m/los_box_task.c (refs OsTskUserStackInit/OsUserTaskEntry
 * live in the arch layer): craft the user context + patch the kernel ctx
 * PC to the OsUserTaskEntry prelude; returns the user stack pointer. */
extern VOID *OsBoxTskUserStackInit(LosTaskCB *taskCB, VOID *userStackBase, UINT32 userStackSize);
/* syscall entry (box/syscalls/los_svc_entry.S) */
extern VOID OsExcSvcCall(VOID);

/* ---- task-module box mechanisms (kernel/base/los_task_box.c, M5B) ---- */
/* refs los_task.c: per-box TCB + kernel-stack pools and the create family. */
extern UINT32 OsBoxTaskInit(UINT16 boxId, LosTaskCB *taskList, LosTaskStack *stackList, UINT32 taskNum);
extern BOOL OsBoxTaskIdCheckInvalid(UINT32 taskId);
extern UINT32 OsBoxTaskGetFreeTaskCB(LosTaskCB **taskCB, const LosBoxCB *boxCB);
extern VOID OsBoxTaskFreeUsedTaskCB(LosTaskCB *taskCB, const LosBoxCB *boxCB);
extern VOID *OsBoxKernelStackAlloc(const LosBoxCB *boxCB, UINT32 stackSize);
extern VOID OsBoxKernelStackFree(LosTaskCB *taskCB);
extern VOID OsBoxKernelStackFreeNode(VOID *stackBase, const LosBoxCB *boxCB);
extern VOID OsBoxUserStackRecycle(LosTaskCB *taskCB);
extern UINT32 OsFirstBoxTaskCreate(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack,
                                   LosBoxCB *boxCB);

/* ---- task dynamic create + user-stack recycle (los_task_box.c, M5D) ----
 * refs los_task.c OsTaskCreateOnlyDynamic (TASK_BASE+38),
 * OsTaskKernelCreateDynamicDo, OsTaskGetRecycleStack (TASK_BASE+39). The
 * usr-side wrappers (LOS_TaskCreateOnly / Los_TaskCreate /
 * LOS_TaskStackRecycle) live in box/usr_demo/usr_api/los_usr_api.c. */
extern UINT32 OsTaskCreateOnlyDynamic(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *usrTopStack);
extern UINT32 OsTaskKernelCreateDynamicDo(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack);
extern VOID  *OsTaskGetRecycleStack(VOID);

/* ---- per-box resource pool init + lifecycle (los_box.c, M5C scaffold) ----
 * refs los_box.c:408-884. Bodies are compile-through stubs until the IPC
 * pool init / SID / check-valid families land. */
extern UINT32 OsBoxResInit(LosBoxCB *box);

/* ---- box lifecycle (los_box.c + los_task_box.c, M5C scaffold) ---- */
extern UINT32 OsGetBoxAndSetStatus(UINT16 boxId, LosBoxCB **boxCB);
extern UINT32 OsBoxRestart(UINT16 boxId);
extern VOID OsBoxDestroy(UINT16 boxId);
extern VOID OsTaskDestroy(LosBoxCB *boxCB);           /* los_task_box.c */

/* SID / swtmr destroy helpers (stubs until the box destroy lifecycle lands).
 * OsSIdBoxInit and the SID module contracts live in los_sec_pri.h
 * (implemented in los_sec.c, M5C). */
extern VOID OsSIdDrvBoxIpcRelease(const LosBoxCB *boxCB);
extern VOID OsSIdBoxResDestroy(const LosBoxCB *boxCB);
extern VOID OsSwtmrDestroy(const LosBoxCB *boxCB);

/* ---- check-valid + user-copy layer (los_box.c, M5D scaffold) ----
 * refs los_box.c:92-372. Scaffold bodies: the WithBox/WithSection/Stack
 * helpers copy refs address-window checks verbatim; the trust-context
 * short-circuit makes the top-level OsCheckValid*Addr / OsCopy* accept any
 * non-NULL pointer until IS_USER_ADDR() is wired (M5D). */
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
