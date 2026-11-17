/*
 * Shim: LosQueueCB lives in the public los_queue.h here, while the box
 * resource config sources include it under this private name.
 */
#ifndef LOS_QUEUE_PRI_H
#define LOS_QUEUE_PRI_H
#include "los_queue.h"
#ifdef LOSCFG_BOX
/* BOX queue infrastructure contracts (los_queue_box.c / los_queue.c):
 * per-box queue free lists over the box resource arrays - same shape as
 * the mux/sem faces. */
#include "los_sec_pri.h"        /* EntityObj / OsSIdModuleInit */

extern LOS_DL_LIST g_freeQueueList;
extern LOS_DL_LIST g_boxFreeQueueList[LOSCFG_BASE_BOX_MAX_LIMIT];

/* implemented in los_queue_box.c */
extern UINT32 OsBoxQueueInit(EntityObj *entityList, LosQueueCB *queList,
                             UINT32 listLen, UINT16 boxId);
extern UINT32 OsBoxQueueListInit(UINT16 boxId);
extern VOID OsGetQueueFreeList(UINTPTR boxCB, LOS_DL_LIST **freeList);
#endif
#endif
