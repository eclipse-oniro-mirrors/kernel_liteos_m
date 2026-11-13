/*
 * los_sec_pri.h — SID (system identifier) module private header.
 *
 * M5C: reduced EntityObj / SIdObj shapes (PERM / RECOVER / SID_LIST are
 * off in this build, same as refs) + the refs verbatim encoding macros,
 * errnos and module contracts (refs kernel_s/base/include/los_sec_pri.h
 * + los_sec.h). refs kernel_s/base/los_sec.c is NOT in the snapshot —
 * the implementation in kernel/base/los_sec.c reconstructs it from these
 * contracts plus the usage pattern in refs safe_ipc/los_queue.c.
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

/* ---- SID errnos (refs los_sec.h; module id outside the OHOS enum,
 * same convention as LOS_MOD_BOX in los_box_pri.h) ---- */
#define LOS_MOD_SEC                      0x2B
#define LOS_ERRNO_SEC_INVALID_BOXID      LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x00)
#define LOS_ERRNO_SEC_INVALID_SID        LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x01)
#define LOS_ERRNO_SEC_PERM_DENIED        LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x02)
#define LOS_ERRNO_SEC_SIDLIST_FULL       LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x03)
#define LOS_ERRNO_SEC_INVALID_SIDMODULE  LOS_ERRNO_OS_ERROR(LOS_MOD_SEC, 0x04)

/* ---- SID module types (refs los_sec.h enum, config-ordered) ---- */
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
    LOS_SID_MOD_NUM
};

/* ---- SID encoding (refs los_sec_pri.h, non-IPC_RECOVER branch) ---- */
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

/* sId layout (reconstruction — refs los_sec.c body is not in the snapshot;
 * consistent with every refs macro and the OsSIdBind / OsSIdObjGet usage in
 * refs safe_ipc/los_queue.c):
 *   bit 31      OS_AUTO_SID_BIT
 *   bits 30..24 boxId (LOS_KERNEL_SID_HEAD = kernel "box")
 *   bits 23..20 module type (LOS_SID_TYPE_*)
 *   bits 19..10 data-CB index inside the module entity array (0 = unbound)
 *   bits  9..0  slot index in the owning SID pool
 * The two 10-bit fields tile OS_SID_ARRAY_MASK + the low bits exactly. */
#define OS_SID_POOL_MASK                0x000003FFU
#define OS_GET_SID_POOLIDX(sId)         ((sId) & OS_SID_POOL_MASK)

/* kernel SID pool: one slot per live kernel resource (mux), allocated as a
 * static array in los_sec.c (refs keeps g_kernelAllSId heap-allocated; the
 * 1:1 mux->sId accounting makes the kernel mux limit the natural size). */
#define LOSCFG_KERNEL_SID_LIMIT         ((UINT32)LOSCFG_BASE_IPC_MUX_LIMIT)

/* Chain-visibility grab point: boxes_res_conf.h includes this shim
 * before boxes_res_auto.cfg, whose externs need LosTaskStack; the
 * usr_demo sources need LOS_TASK_STACK_ALIGN at file scope.
 * M5B: 0x400, not the refs 0x200 - the OHOS blocking-syscall chain
 * (svc save + graft frame + scheduler frames + PendSV nesting) measures
 * ~500B deep; 0x200 overflowed the pool stack into the neighboring
 * .box.res data (QEMU crash after the first TaskDelay). */
#define LOSCFG_USR_THREAD_KERNEL_STACK_SIZE 0x400
#define LOS_TASK_STACK_ALIGN __attribute__((aligned(8)))

typedef struct LosTaskStackTag {
    LOS_TASK_STACK_ALIGN UINT8 stack[LOSCFG_USR_THREAD_KERNEL_STACK_SIZE];
    LOS_DL_LIST pendList;
} LosTaskStack;
#define OS_SID_NAME_MAX_LEN     ((UINT32)LOSCFG_SID_NAME_MAX_LEN)

typedef struct {
    CHAR regName[OS_SID_NAME_MAX_LEN];
    UINT16 owner;
    VOID *moduleData;
} EntityObj;

typedef struct {
    UINT32 sId;
    EntityObj *entity;
    OS_SL_LIST ipcList;
} SIdObj;

/* ---- module handle (refs SIdModuleHandle, reduced: destroyCb only;
 * IPC_RECOVER callbacks are compiled out) ---- */
typedef UINT32 (*FuncDestroy)(const VOID *boxCB, const VOID *entityCB, VOID *recoverCB);
typedef struct {
    FuncDestroy destroyCb;
} SIdModuleHandle;

/* ---- module registry entry (refs SIdModule, reduced) ---- */
typedef struct {
    SIdModuleHandle cbHdl;
    EntityObj *moduleEntityArr;
    UINT32 listSize;
} SIdModule;

/* ---- SID module contracts (refs los_sec_pri.h) ---- */
extern SIdObj *g_kernelAllSId;
extern VOID OsSIdListInit(VOID);
extern UINT32 OsSIdBoxInit(LOS_DL_LIST *sidOwnerList, LOS_DL_LIST *sidFreeList,
    SIdObj *sIdArr, UINT32 sIdSize);
extern SIdModule *OsSIdModListGet(UINT32 type, UINT16 boxId);
extern UINT32 OsSIdModuleInit(UINT32 moduleType, UINT8 permType,
    EntityObj *entityList, UINT32 listLen, UINT16 boxId);
extern UINT32 OsSIdValidate(UINT32 sId);
extern UINT32 OsSIdOwnerCheck(UINT32 sId);
extern UINT32 OsSIdKernelResIntCheck(UINT32 sId);
extern UINT32 OsSIdOwnerAndTypeCheck(UINT32 sId, UINT32 moduleType);
extern UINT32 OsEntityOwnerCheck(const EntityObj *entityObj);
extern UINT32 OsEntityOwnerCheckbySId(UINT32 sId);
extern UINT32 OsSIdBind(UINT32 type, SIdObj *sIdObj, UINT32 dataCBIdx, VOID *dataCB);
extern UINT32 OsSIdGenObj(UINT32 type, UINT32 dataCBIdx, VOID *dataCB, UINT32 *sId);
extern UINT32 OsSIdObjGet(UINT32 sId, SIdObj **outSIdObj);
extern UINT32 OsSIdGetDataCB(UINT32 sId, UINT32 moduleType, VOID **dataCB);
extern VOID OsSIdRecycle(const EntityObj *entityObj, VOID *recoverCB);
extern UINT32 OsSIdObjDel(UINT32 sId);
extern UINT32 OsEntityOwnerGetBySid(UINT32 sId);

/* box SID free lists live here (refs keeps them in the box CB only under
 * LOSCFG_BASE_SID_LIST; the reduced refs passes NULL/NULL to OsSIdBoxInit,
 * so the internal lists are the only consistent shape). */
extern OS_SL_LIST g_boxSIdFreeList[LOSCFG_BASE_BOX_MAX_LIMIT];

#ifdef __cplusplus
}
#endif

#endif /* LOS_SEC_PRI_H */
