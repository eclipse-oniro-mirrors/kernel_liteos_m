/*
 * los_box_pri.h — BOX feature private header, adapted port from
 * refs/liteos/kernel_s/base/include/los_box_pri.h.
 *
 * Adaptations for the OHOS liteos_m base (M2, faithful-first):
 *  - includes resolved to OHOS headers + local shims (los_sec_pri.h);
 *  - target-layer constants missing from refs are defined here
 *    (LOSCFG_BASE_BOX_MAX_LIMIT / OS_BOX_MEM_LAYOUT_NUM / LOS_BOX_BASE_ID /
 *    BOX_RES_START|END placeholder / LOSCFG_USR_THREAD_KERNEL_STACK_SIZE);
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
#include "los_sec_pri.h"
#include "asm/memmap_config.h"    /* SYS_USR_* glue + slot geometry */

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
#define LOS_BOX_BASE_ID                 1   /* user box ids start at 1 */
#endif
/* M2 placeholder: per-box .box.res span, wired for real at M5 */
#define BOX_RES_START                   0
#define BOX_RES_END                     0

#define OS_BOX_SERVICE_LIMIT            10

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
#define BOX_SET_TIME_RIGHT(boxStatus)   ((boxStatus) & OS_BOX_SET_TIME == OS_BOX_SET_TIME)
#define BOX_IS_NOLOAD(boxStatus)        ((boxStatus) & OS_BOX_NOLOAD == OS_BOX_NOLOAD)
#define BOX_IS_INIT(boxStatus)          ((boxStatus) & BOX_INIT_STATUS_MASK == OS_BOX_INIT)
#define BOX_IS_INITING(boxStatus)       ((boxStatus) & BOX_INIT_STATUS_MASK == OS_BOX_INITING)
#define BOX_IS_UNINIT(boxStatus)        ((boxStatus) & BOX_INIT_STATUS_MASK == OS_BOX_UNINIT)
#define BOX_IS_TRUST(boxStatus)         ((boxStatus) & OS_BOX_TRUST == OS_BOX_TRUST)
#define BOX_SET_INIT_STATUS(boxStatus, initStatus) \
    ((boxStatus) = ((boxStatus) & ~OS_BOX_INIT_STATUS_MASK) | (initStatus))

#define OS_BOX_GET_BID(id)              ((((id) >> 16) & (UINT32)0xFFFF) - (UINT32)1)
#define OS_BOX_GET_ID(id)               ((id) & (UINT32)0xFFFF)
#define OS_BOX_SET_ID(boxId, id)        (((UINT32)((boxId) + (UINT32)1) << 16) | (id & (UINT32)0xFFFF))

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
/* owned by los_box_mini.c, consumed by the M3/M4 ported asm and veneer */
extern UINT32 g_sysMode;                            /* 0 kernel / 1 user thread */
extern volatile UINT16 g_curBoxId;                  /* id of the running box */
extern UINTPTR g_boxLocalPool[OS_BOX_MEM_LAYOUT_NUM];

/* ---- entry points (los_box_mini.c / los_box_mpu.c) ---- */
extern UINT32 OsBoxInit(LosBoxCB * const userBoxArr, SysMemLayout * const sysMemLayoutArr);
extern LosBoxCB *OsGetCurBoxCB(VOID);
extern VOID MpuLayoutInit(VOID);
extern VOID BoxMpuSwitch(const LosBoxCB *boxCB);
/* syscall entry (box/syscalls/los_svc_entry.S) */
extern VOID OsExcSvcCall(VOID);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LOS_BOX_PRI_H */
