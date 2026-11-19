/*
 * los_sec_pri.h — SID (system identifier) module private header.
 *
 * Reduced EntityObj / SIdObj shapes (PERM / RECOVER / SID_LIST are off in
 * this build) plus the SID encoding macros, errnos and module contracts.
 * The implementation lives in kernel/box/los_sec.c.
 */
#ifndef LOS_SEC_PRI_H
#define LOS_SEC_PRI_H

#include "los_typedef.h"
#include "los_list.h"
#include "los_singly_list_pri.h"
#include "los_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOSCFG_SID_NAME_MAX_LEN 16

/* ---- SID errnos (module id outside the kernel enum, same convention as
 * LOS_MOD_BOX in los_box_pri.h) ---- */
#define LOS_MOD_SEC                      0x2B
#define LOS_ERRNO_SEC_INVALID_BOXID      LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x00)
#define LOS_ERRNO_SEC_INVALID_SID        LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x01)
#define LOS_ERRNO_SEC_PERM_DENIED        LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x02)
#define LOS_ERRNO_SEC_SIDLIST_FULL       LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x03)
#define LOS_ERRNO_SEC_INVALID_SIDMODULE  LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x04)
/* refs 0x09. The refs codes between (0x05 PERM_OP, 0x06 ADDR, 0x07 SID_NAME,
 * 0x08 NOT_ALLOW_IN_ISR) and after (0x0A RESOURCE_UPDATED, 0x0B RECOVERINFG)
 * belong to the PERM / SID_LIST / RECOVER blocks and are added with them. */
#define LOS_ERRNO_SEC_RESOURCE_NONE      LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x09)

/* ---- SID module types (config-ordered) ---- */
enum {
#ifdef LOSCFG_BASE_IPC_MUX
    LOS_SID_TYPE_MUX,
#endif
#ifdef LOSCFG_BASE_IPC_SEM
    LOS_SID_TYPE_SEM,
#endif
#ifdef LOSCFG_BASE_IPC_QUEUE
    LOS_SID_TYPE_QUE,
#endif
#ifdef LOSCFG_BASE_IPC_EVENT
    LOS_SID_TYPE_EVT,
#endif
#ifdef LOSCFG_BASE_IPC_CONDVAR
    LOS_SID_TYPE_COND,
#endif
    LOS_SID_MOD_NUM
};

/* ---- SID encoding (non-IPC_RECOVER branch) ---- */
#define OS_AUTO_SID_BIT                 0x80000000U
#define LOS_KERNEL_SID_HEAD             0x7FU
#define OS_SID_BOXID_MASK               0x7F000000U
#define OS_SID_TYPE_MASK                0x00F00000U
#define OS_SID_ARRAY_MASK               0x000FFC00U
#define OS_SID_BIT_SIZE                 8U
#define OS_SID_BITMAP_SIZE              ((((UINT32)LOSCFG_BASE_BOX_MAX_LIMIT - (UINT32)1) / (UINT32)OS_SID_BIT_SIZE) + (UINT32)1)

#define OS_SID_RES_DESTROYED            0U
#define OS_SID_RES_UNDESTROYED          1U

#define OS_SID_BOX_SHIFT                24U
#define OS_SID_TYPE_SHIFT               20U
#define OS_SID_ARRAY_SHIFT              10U
#define OS_GET_SID_BOXID(sId)           (UINT16)(((sId) & OS_SID_BOXID_MASK) >> OS_SID_BOX_SHIFT)
#define OS_GET_SID_TYPE(sId)            (((sId) & OS_SID_TYPE_MASK) >> OS_SID_TYPE_SHIFT)
#define OS_SID_BID_IS_KER(sId)          (OS_GET_SID_BOXID(sId) == LOS_KERNEL_SID_HEAD)

/* sId layout, as used by OsSIdBind / OsSIdObjGet and every macro above:
 *   bit 31      OS_AUTO_SID_BIT
 *   bits 30..24 boxId (LOS_KERNEL_SID_HEAD = kernel "box")
 *   bits 23..20 module type (LOS_SID_TYPE_*)
 *   bits 19..10 slot index in the owning SID pool (OS_SID_ARRAY_MASK)
 *   bits  9..0  auto salt (OsSIdGetAutoIndex: a monotonic counter masked to
 *               this field, so a recycled slot does not reproduce a sId that
 *               is still circulating)
 * The data-CB index is NOT in the sId: OsSIdBind stores the entity pointer in
 * the slot and the CB hangs off entity->moduleData, so resolution needs no
 * index field at all. */
/* Decode the pool-slot field. The low 10 bits (the auto salt) are never
 * decoded - they only guarantee that a recycled slot hands out a different
 * sId than the one still circulating from its previous life. */
#define OS_GET_SID_ARRIDX(sId)          (((sId) & OS_SID_ARRAY_MASK) >> OS_SID_ARRAY_SHIFT)

/* Kernel SID pool: one slot per live kernel resource. Every module type that
 * can be created in kernel context draws a slot from it, so the pool is the
 * SUM of the kernel-table limits - not just mux, which was the natural size
 * only while the SIdObj machinery was parked and mux was the sole binder.
 * The queue STATIC pool is excluded: under BOX a static CB cannot pass the
 * create exit's OsSIdGenObj bind, so it never owns a slot. */
#ifdef LOSCFG_BASE_IPC_MUX
#define OS_KSID_MUX_LIMIT               ((UINT32)LOSCFG_BASE_IPC_MUX_LIMIT)
#else
#define OS_KSID_MUX_LIMIT               0U
#endif
#ifdef LOSCFG_BASE_IPC_SEM
#define OS_KSID_SEM_LIMIT               ((UINT32)LOSCFG_BASE_IPC_SEM_LIMIT)
#else
#define OS_KSID_SEM_LIMIT               0U
#endif
#ifdef LOSCFG_BASE_IPC_QUEUE
#define OS_KSID_QUE_LIMIT               ((UINT32)LOSCFG_BASE_IPC_QUEUE_LIMIT)
#else
#define OS_KSID_QUE_LIMIT               0U
#endif
#if (LOSCFG_BASE_IPC_EVENT == 1)
#define OS_KSID_EVT_LIMIT               ((UINT32)LOSCFG_BASE_IPC_EVENT_LIMIT)
#else
#define OS_KSID_EVT_LIMIT               0U
#endif
#ifdef LOSCFG_BASE_IPC_CONDVAR
#define OS_KSID_COND_LIMIT              ((UINT32)LOSCFG_BASE_IPC_CONDVAR_LIMIT)
#else
#define OS_KSID_COND_LIMIT              0U
#endif
#define LOSCFG_KERNEL_SID_LIMIT         (OS_KSID_MUX_LIMIT + OS_KSID_SEM_LIMIT + \
                                         OS_KSID_QUE_LIMIT + OS_KSID_EVT_LIMIT + \
                                         OS_KSID_COND_LIMIT)

/* Chain-visibility grab point: boxes_res_conf.h includes this shim
 * before boxes_res_auto.cfg, whose externs need LosTaskStack; the
 * usr_demo sources need LOS_TASK_STACK_ALIGN at file scope.
 * 0x400 is a measured floor, not a round number: the blocking-syscall
 * chain (svc save + graft frame + scheduler frames + PendSV nesting)
 * measures ~500B deep, and a 0x200 pool stack overflows into the
 * neighboring .box.res data - a crash right after the first TaskDelay. */
#define LOSCFG_USR_THREAD_KERNEL_STACK_SIZE 0x400
#define LOS_TASK_STACK_ALIGN __attribute__((aligned(8)))

typedef struct LosTaskStackTag {
    LOS_TASK_STACK_ALIGN UINT8 stack[LOSCFG_USR_THREAD_KERNEL_STACK_SIZE];
    LOS_DL_LIST pendList;
} LosTaskStack;
#define OS_SID_NAME_MAX_LEN     ((UINT32)LOSCFG_SID_NAME_MAX_LEN)

/* refs EntityObj with LOSCFG_BASE_SID_LIST (sIdNameList) and
 * LOSCFG_BASE_IPC_PERM (permList) compiled out. userSIdList is the ALIAS
 * chain: every SIdObj bound to this entity links in through its own ipcList,
 * so OsSIdRecycle can unbind all of them in one walk. regName is provisioned
 * but unused until the naming service (LOS_SIdRegName) is ported. */
typedef struct {
    OS_SL_LIST userSIdList;
    CHAR regName[OS_SID_NAME_MAX_LEN];
    UINT16 owner;
    VOID *moduleData;
} EntityObj;

/* refs SIdObj with LOSCFG_BASE_SID_LIST (ownerSIdList) and
 * LOSCFG_BASE_IPC_RECOVER (the flags:2 / sId:30 bitfield) compiled out.
 * ipcList is this slot's link in its entity's userSIdList - NOT a free-list
 * link. With BASE_SID_LIST off there is no free list at all: a slot is free
 * exactly when sId == 0, which is what OsSIdGetFreeSIdObj scans for and what
 * OsSIdClear restores. */
typedef struct {
    UINT32 sId;
    EntityObj *entity;
    OS_SL_LIST ipcList;
} SIdObj;

/* OsSIdObjDelDo's recycle-callback return: whether the entity belonged to the
 * box being torn down (and so was fully recycled) or to someone else (and so
 * was only unbound). refs spelling kept for grep-ability. */
#define IS_CURRENT_BOX_RES              0
#define IS_OTHRE_BOX_RES                1

/* ---- module handle (reduced: destroyCb only;
 * IPC_RECOVER callbacks are compiled out) ---- */
typedef UINT32 (*FuncDestroy)(const VOID *boxCB, const VOID *entityCB, VOID *recoverCB);
typedef struct {
    FuncDestroy destroyCb;
} SIdModuleHandle;

/* ---- module registry entry (reduced) ---- */
typedef struct {
    SIdModuleHandle cbHdl;
    EntityObj *moduleEntityArr;
    UINT32 listSize;
} SIdModule;

/* ---- SID module contracts ---- */
extern SIdObj *g_kernelAllSId;
extern VOID OsSIdListInit(VOID);
extern UINT32 OsSIdBoxInit(LOS_DL_LIST *sidOwnerList, LOS_DL_LIST *sidFreeList,
    SIdObj *sIdArr, UINT32 sIdSize);
extern SIdModule *OsSIdModListGet(UINT32 type, UINT16 boxId);
extern UINT32 OsSIdModuleInit(UINT32 moduleType, UINT8 permType,
    EntityObj *entityList, UINT32 listLen, UINT16 boxId);
extern UINT32 OsSIdValidate(UINT32 sId);
extern UINT32 OsSIdOwnerCheck(UINT32 sId);
/* A box may USE a kernel-owned resource (pend/post the box-heap lock mux) but
 * may not CLAIM or DESTROY it. NOTE the name: refs has a function called
 * OsSIdKernelResIntCheck with the OPPOSITE meaning ("in an ISR only
 * kernel-head sIds are valid"), belonging to its pre-generated-sId create
 * path (LOS_GenSId + OsXxxCreate with *sId != 0) which this tree does not
 * implement. The refs name is deliberately left unused here rather than
 * reused for different semantics - same trap as refs' self-locking
 * OsMuxGetCBLocked vs our caller-locked OsMuxGetCB. */
extern UINT32 OsSIdKernelResClaimCheck(UINT32 sId);
/* refs OsSIdKernelResIntCheck, verbatim and now with a real caller
 * (OsCondVarCreate's pre-generated-sId arm): in an interrupt only kernel-head
 * resources may be touched. This is the OPPOSITE concern from
 * OsSIdKernelResClaimCheck above - the two names are easy to confuse, so both
 * are spelled out rather than merged. */
extern UINT32 OsSIdKernelResIntCheck(UINT32 sId);
extern UINT32 OsSIdOwnerAndTypeCheck(UINT32 sId, UINT32 moduleType);
extern UINT32 OsEntityOwnerCheck(const EntityObj *entityObj);
extern UINT32 OsEntityOwnerCheckbySId(UINT32 sId);
extern UINT32 OsSIdBind(UINT32 type, SIdObj *sIdObj, UINT32 dataCBIdx, VOID *dataCB);
extern UINT32 OsSIdGenObj(UINT32 type, UINT32 dataCBIdx, VOID *dataCB, UINT32 *sId);
extern UINT32 OsSIdObjGet(UINT32 sId, SIdObj **outSIdObj);
extern UINT32 OsSIdGetDataCB(UINT32 sId, UINT32 moduleType, VOID **dataCB);
/* Public sId hand-out/release pair (los_sec.c). LOS_GenSId had no in-tree
 * caller until the M6T queue port; the declarations live here - next to
 * every other sId contract - instead of a public header (the usr world
 * reaches the sId world through the LOS_* resource APIs, not these). */
extern UINT32 LOS_GenSId(UINT32 type);
extern UINT32 LOS_SIdDelete(UINT32 sId);
/* Entry gate - normalize a SID handle into its pool index, in place.
 * idOffset/idWidth describe the pool-handle field in the CB, and the width
 * matters: LosQueueCB.queueID is UINT16 at a 2-mod-4 offset, so reading it
 * as UINT32 there is an unaligned access that traps. */
extern UINT32 OsIpcSecIdNorm(UINT32 *handle, UINT32 sidType, UINT32 idOffset, UINT32 idWidth);
#ifdef LOSCFG_BASE_IPC_QUEUE
/* Queue create exit - bind the SID, reclaim on failure. */
extern UINT32 OsQueueSecCreateOut(UINT32 *queueID, VOID *cb, VOID *queueMem,
                                  LOS_DL_LIST *freeList, BOOL isStatic);
#endif
extern VOID OsSIdRecycle(const EntityObj *entityObj, VOID *recoverCB);
extern UINT32 OsSIdObjDel(UINT32 sId);
extern UINT32 OsEntityOwnerGetBySid(UINT32 sId);

/* refs provides a real OsSIdRegModuleHandle only under LOSCFG_EXC_ISOLATION
 * (it records a per-module destroyCb for the box-teardown walk); with that off
 * refs supplies this empty inline so module init can call it unconditionally.
 * Same here - LOSCFG_EXC_ISOLATION is not configured in this build, so no
 * destroyCb is ever registered and OsSIdBoxResDestroy uses the plain
 * OsSIdRecycleByDel path instead. */
STATIC INLINE VOID OsSIdRegModuleHandle(UINT32 type, UINT16 boxId, const SIdModuleHandle *handle)
{
    (VOID)type;
    (VOID)boxId;
    (VOID)handle;
}

#ifdef __cplusplus
}
#endif

#endif /* LOS_SEC_PRI_H */
