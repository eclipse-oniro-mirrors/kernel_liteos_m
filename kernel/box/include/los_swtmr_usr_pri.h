/*
 * los_swtmr_usr_pri.h — per-box software-timer context.
 *
 * The kernel keeps one context per box: the box-provided timer pool
 * (attached by the box core when the box is created, OsSwtmrUsrBoxInit) and
 * the deferred callback ring the kernel scanner produces into.
 */
#ifndef _LOS_SWTMR_USR_PRI_H
#define _LOS_SWTMR_USR_PRI_H

#include "los_typedef.h"
#include "los_list.h"
#include "los_swtmr_pri.h"

/* Guard for everything below: the box swtmr extension is active only when
 * the isolation feature AND the kernel swtmr module are both configured.
 * Neither macro alone suffices - LOSCFG_BASE_CORE_SWTMR stays defined in
 * BOX=off builds, and LOSCFG_BOX alone would reference a swtmr module that
 * might not be compiled in. */

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)

/* Ring buffer of deferred swtmr callbacks, one per box. The kernel scanner
 * produces items (value copies) and the owning box's swtmr task consumes
 * them; layout mirrors the box-core ExpiryList. */
typedef struct {
    UINT16 head;
    UINT16 tail;
    UINT16 len;
    UINT16 freeCnt;
    UINTPTR *buf;
} BoxSwtmrQueue;

/* Kernel-side per-box swtmr context. The cb array and its queue buffer are
 * provided by the box core when a box is created (OsSwtmrUsrBoxInit). */
typedef struct {
    BoxSwtmrQueue swtmrQueue;
    LOS_DL_LIST freeList;
    LosSwtmrCB *cbArray;
    UINT32 cbArrayLen;
    UINT32 swtmrTaskId;
} SwtmrBoxCtx;

extern SwtmrBoxCtx g_swtmrBoxCtx[LOSCFG_BASE_BOX_MAX_LIMIT];

/* Attach a box-provided swtmr pool and callback queue to a box. */
extern UINT32 OsSwtmrUsrBoxInit(UINT16 boxId, LosSwtmrCB *swtmList, UINT32 listLen,
                                UINTPTR queueBuf, UINT32 queueLen);

/* Create the per-box swtmr task. taskStack NULL allocates the user stack
 * from the box heap (dynamic); non-NULL runs the task on the caller stack
 * (static discriminator). */
extern UINT32 OsSwtmrUsrTaskInitEx(UINT16 boxId, VOID *taskStack, UINT32 stackSize);

/* Same with the box-heap stack: the box entry's BoxSwtmrInit path. */
extern UINT32 OsSwtmrUsrTaskInit(UINT16 boxId, UINT32 stackSize);

/* refs-prototype bring-up of the CURRENT box's swtmr task (syscall face):
 * the box is taken from the caller context, so no boxId argument. */
extern UINT32 LOS_SwtmrUsrInit(VOID *taskStack, UINT32 stackSize);

/* Resolve the LosSwtmrCB behind an (encoded) box swtmr id: NULL when the
 * id does not address a live box pool timer (out of range / stale owner).
 * Needed because OsSwtmrIdVerify in los_swtmr.c only walks the kernel
 * array: box timers carry an OS_BOX_SET_ID-encoded id and resolve here. */
extern LosSwtmrCB *OsSwtmrFromIdUsr(UINT32 swtmrId);

#endif /* LOSCFG_BOX && LOSCFG_BASE_CORE_SWTMR */

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _LOS_SWTMR_USR_PRI_H */
