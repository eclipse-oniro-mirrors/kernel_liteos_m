/*
 * los_event_sid.h — event access addressed by id (the SID family), beside
 * the pointer family in los_event.h:
 *   - pool family: an event out of the owning box's pool, addressed by flat
 *     sId (LOS_EventCreate / LOS_EventReadWithSId / ...);
 *   - task family: LOS_TaskEventRead / LOS_TaskEventWrite addressing the
 *     event embedded in LosTaskCB (read = current task, write = any task).
 *     This is the wake-up primitive of the per-box software-timer task.
 *
 * Guard: both families are BOX machinery (the timer task lives in a box);
 * BOX=off builds see nothing.
 */
#ifndef _LOS_EVENT_SID_H
#define _LOS_EVENT_SID_H

#include "los_typedef.h"

#if defined(LOSCFG_BOX) && (LOSCFG_BASE_IPC_EVENT == 1)
#include "los_event.h"      /* EVENT_CB_S (LosEventCB first member) */
#include "los_sec_pri.h"    /* EntityObj (box-core hook below). Safe in both
                             * worlds: box resource tables already name
                             * EntityObj/SIdObj through boxes_res_conf.h, so
                             * this leaks no kernel-only type. */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Error codes: module LOS_MOD_EVENT, 0x20..0x24 (0x21 unused) */
#define LOS_ERRNO_EVENT_SEC_MAX_NUM_LIMIT   LOS_ERRNO_OS_ERROR(LOS_MOD_EVENT, 0x20)
#define LOS_ERRNO_EVENT_SEC_DEL_SID_OBJ     LOS_ERRNO_OS_ERROR(LOS_MOD_EVENT, 0x22)
#define LOS_ERRNO_EVENT_SEC_TSK_INVALID     LOS_ERRNO_OS_ERROR(LOS_MOD_EVENT, 0x23)
#define LOS_ERRNO_EVENT_SEC_ID_TYPE_INVALID LOS_ERRNO_OS_ERROR(LOS_MOD_EVENT, 0x24)

/* Pool event node. EVENT_CB_S MUST stay the first member: it makes
 * &node->eventNode == node, and the flat-sId resolution in los_sec.c does
 * pool array arithmetic in terms of EVENT_CB_S. */
typedef struct {
    EVENT_CB_S eventNode;
    UINT32 eventCBId;         /* pool index, stamped at pool init */
} LosEventCB;

/* SID-addressed event target types (both used by the task family) */
#define OS_EVENT_ID_TYPE_EVENT   0  /* EVENT-pool object                 */
#define OS_EVENT_ID_TYPE_TASK    1  /* sId is a task id (write path only) */
#define OS_EVENT_ID_TYPE_CURTASK 2  /* sId ignored, current task         */

/* Write/clear return modes */
#define OS_EVENT_MODE_RETURN_BEFORE 0
#define OS_EVENT_MODE_RETURN_AFTER  1
#define OS_EVENT_MODE_RETURN_NONE   2

/**
 * @brief Read the event set embedded in the CURRENT task (read-and-clear,
 *        OR match). Same preconditions as LOS_EventRead.
 * @return the matched event set, or an error code on failure/timeout.
 */
extern UINT32 LOS_TaskEventRead(UINT32 event, UINT32 timeout);

/**
 * @brief Write an event set to the event embedded in the target task.
 *        Wakes matching waiters; usable from interrupts and any task.
 * @return LOS_OK, or an error code.
 */
extern UINT32 LOS_TaskEventWrite(UINT32 taskId, UINT32 event);

/* box-core hooks (OsBoxResEventInit), mirroring OsBoxSemInit/OsBoxSemListInit:
 * register the box entity array with the SID module and build the box free
 * list over the CB array. Implemented in los_event_sid.c. */
extern UINT32 OsBoxEventInit(EntityObj *entityList, LosEventCB *evtList,
                             UINT32 listLen, UINT16 boxId);
extern UINT32 OsBoxEventListInit(UINT16 boxId);

/* ---- kernel-side SID event table ----
 * The id-addressed family is two-bounded exactly like sem/mux/queue: a kernel
 * table plus one pool per box. A kernel-created event therefore gets a
 * kernel-head sId that a box can resolve, which is what makes "kernel creates,
 * user reads/writes by id" work. The pointer-addressed family in the baseline
 * los_event.c does not touch any of this.
 *
 * Only the table pointer is exported here (los_sec.c's EVT arms resolve
 * through it). The kernel free list and the context-split helper stay STATIC
 * in los_event_sid.c: nothing outside that TU needs them, and exporting the
 * helper would drag LosBoxCB into a header that box user code includes. */
extern LosEventCB *g_osAllEvent;

/* OsIpcInit callee: allocate and link up the kernel table. */
extern UINT32 OsEventInit(VOID);

/* Pointer-family bodies over a caller-held EVENT_CB_S, with the scheduler lock
 * owned by the CALLER (so a composite object can hold the lock across its own
 * bookkeeping). These are the copies los_event_sid.c already maintains for the
 * sId family; the condvar embeds an EVENT_CB_S in each LosCondVarCB and waits
 * / signals through them. *needSched reports whether a LOS_Schedule() is owed
 * once the lock is dropped. */
extern UINT32 OsEventReadSid(PEVENT_CB_S eventCB, UINT32 eventMask, UINT32 mode, UINT32 timeout);
extern UINT32 OsEventWriteLockedSid(PEVENT_CB_S eventCB, UINT32 events, BOOL once, BOOL *needSched);

/* ---- SID event-pool family ----
 * Events come from the owning box's pool (boxes_cfg EVENT_LIMIT), are
 * addressed by sId through the SID module, and are returned on delete. */
extern UINT32 LOS_EventCreate(UINT32 *sId);
extern UINT32 LOS_EventDelete(UINT32 sId);
extern UINT32 LOS_EventPollWithSId(UINT32 sId, UINT32 eventMask, UINT32 mode);
extern UINT32 LOS_EventReadWithSId(UINT32 sId, UINT32 eventMask, UINT32 mode, UINT32 timeout);
extern UINT32 LOS_EventWriteWithSId(UINT32 sId, UINT32 events);
extern UINT32 LOS_EventWriteOnceWithSId(UINT32 sId, UINT32 events);
extern UINT32 LOS_EventClearWithSId(UINT32 sId, UINT32 events);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LOSCFG_BOX && LOSCFG_BASE_IPC_EVENT */

#endif /* _LOS_EVENT_SID_H */
