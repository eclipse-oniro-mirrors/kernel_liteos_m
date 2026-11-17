/*
 * los_condvar_pri.h — BOX condition variable (ported from refs
 * kernel_s/base/safe_ipc/los_condvar.c).
 *
 * A condvar is a pool object addressed by sId, exactly like sem/mux/queue/
 * event: two bounds (a kernel table plus one pool per box), one SID module
 * slot per live object, and an EntityObj array parallel to the CB array in
 * each bound. What makes it different is that it is BUILT OUT OF the other
 * two primitives - each node embeds an EVENT_CB_S for its waiter queue and it
 * is always used together with a mutex sId:
 *
 *   OsCondVarWait(condSId, muxSId, ticks)
 *       resolve the mux (self-locking OsMuxGetCBLocked - the wait must not
 *       hold the scheduler lock across the mux release)
 *       sample cond->value, bump cond->count
 *       release the mux, block on the embedded event, re-take the mux
 *       on a timeout, undo the count bump
 *
 *   OsCondVarSignal(condSId, isBroadcast)
 *       decrement (or zero, for broadcast) cond->count, bump the value
 *       counter atomically, then write the event - once=TRUE for signal,
 *       once=FALSE for broadcast
 *
 * The value counter is what makes the classic lost-wakeup race detectable:
 * the waiter samples it before releasing the mux and re-checks it after, so a
 * signal that landed in between is seen without needing to stay blocked.
 *
 * This header is included by box user code (the demo calls the four
 * OsCondVarXxx entries through their veneers), so it must not name any
 * kernel-world-only type - OsGetCondVarFreeList takes a LosBoxCB and stays
 * STATIC in the .c for that reason (same rule that keeps OsGetEventFreeList
 * out of los_event_sid.h).
 */
#ifndef _LOS_CONDVAR_PRI_H
#define _LOS_CONDVAR_PRI_H

#include "los_typedef.h"
#include "los_list.h"
#include "los_event.h"      /* EVENT_CB_S (LosCondVarCB first member) */
#include "los_sec_pri.h"    /* EntityObj (box-core hook below) */

#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_IPC_CONDVAR)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Module id outside the kernel LOS_MOD_* enum, same convention as LOS_MOD_BOX
 * (0x2A) and LOS_MOD_SEC (0x2B). */
#define LOS_MOD_COND                      0x2C

/* refs los_condvar_pri.h errnos, same codes. The refs spelling
 * "SHOULD_NOT_DESTORY" is kept so the two trees stay greppable against each
 * other. */
#define LOS_ERRNO_COND_READ_IN_INTERRUPT  LOS_ERRNO_OS_ERROR(LOS_MOD_COND, 0x00)
#define LOS_ERRNO_COND_PTR_NULL           LOS_ERRNO_OS_ERROR(LOS_MOD_COND, 0x01)
#define LOS_ERRNO_COND_SHOULD_NOT_DESTORY LOS_ERRNO_OS_ERROR(LOS_MOD_COND, 0x02)
#define LOS_ERRNO_COND_MAX_NUM_LIMIT      LOS_ERRNO_OS_ERROR(LOS_MOD_COND, 0x03)
#define LOS_ERRNO_COND_INVAL_ADDR         LOS_ERRNO_OS_ERROR(LOS_MOD_COND, 0x04)
#define LOS_ERRNO_COND_INIT_SID           LOS_ERRNO_OS_ERROR(LOS_MOD_COND, 0x05)
#define LOS_ERRNO_COND_DEL_SID_OBJ        LOS_ERRNO_OS_ERROR(LOS_MOD_COND, 0x06)
/* refs files this one under LOS_MOD_MUX, not LOS_MOD_COND - kept as refs has
 * it so the numeric value matches across the two trees. */
#define LOS_ERRNO_COND_NOT_OWNER_PEND     LOS_ERRNO_OS_ERROR(LOS_MOD_MUX, 0x07)

/* refs writes LOS_DL_LIST__ENTRY here (double underscore), which is not a
 * macro that exists; the intended one is LOS_DL_LIST_ENTRY. The field is
 * stEventList in this tree (refs calls it eventList). */
#define GET_COND_EVENT_LIST(ptr) LOS_DL_LIST_ENTRY(ptr, EVENT_CB_S, stEventList)

/* Condvar pool node. EVENT_CB_S MUST stay the first member: it makes
 * &node->eventNode == node, so the SID entity's moduleData can be read as
 * either spelling (the same reason LosEventCB is shaped that way).
 *
 * condCBId carries the POOL INDEX until create binds it, which is the
 * dataCBIdx OsSIdBind needs - unlike sem/mux it is never overwritten with the
 * sId (nothing here has to compare a handle against the CB, because the SID
 * slot's exact-sId comparison already does the stale-handle job). */
typedef struct {
    EVENT_CB_S eventNode;
    UINT32 condCBId;
    INT32 count;      /* waiters registered by OsCondVarWait */
    INT32 value;      /* low 2 bits: COND_FLAGS_*; above: the change counter */
} LosCondVarCB;

/* Kernel-side table (the other bound). Allocated from the system heap by
 * OsCondInit, mirroring g_osAllSem / g_osAllEvent. The kernel entity array
 * stays STATIC in los_condvar.c - nothing outside that TU needs it. */
extern LosCondVarCB *g_osAllCond;

/* OsIpcInit callee: allocate the kernel table and register the module. */
extern UINT32 OsCondInit(VOID);

/* Box-core hooks (OsBoxResCondInit), mirroring OsBoxSemInit/OsBoxSemListInit:
 * register the box entity array with the SID module and build the box free
 * list over the CB array. */
extern UINT32 OsBoxCondInit(EntityObj *entityList, LosCondVarCB *condList,
                            UINT32 listLen, UINT16 boxId);
extern UINT32 OsBoxCondListInit(UINT16 boxId);

/* ---- the four box-facing entries (syscall-exposed, see syscalls_def.h) ---- */
extern UINT32 OsCondVarCreate(UINT32 *sId);
extern UINT32 OsCondVarDelete(UINT32 sId);
extern UINT32 OsCondVarSignal(UINT32 sId, BOOL isBroadcast);
extern UINT32 OsCondVarWait(UINT32 sId, UINT32 mutexId, UINT32 absTicks);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LOSCFG_BOX && LOSCFG_BASE_IPC_CONDVAR */

#endif /* _LOS_CONDVAR_PRI_H */
