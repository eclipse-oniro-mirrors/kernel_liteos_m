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

/* ---- refs port (M6T): LOS_QueueCreateWithSId / LOS_QueueInfoAllGet ----
 * Error codes, the export struct and the wait-task bitmap macros are refs
 * verbatim (kernel_s include/los_queue.h 0x30..0x37 tail + base/include/
 * los_queue_pri.h); they live in this private shim so the upstream public
 * header stays untouched. Promote them together with the syscall veneers
 * if these ever need to reach the usr world (refs lists both APIs on its
 * kernel syscall surface). */

/* refs los_queue.h, only the four this port consumes. */
#define LOS_ERRNO_QUEUE_INVALID_ADDR       LOS_ERRNO_OS_ERROR(LOS_MOD_QUE, 0x30)
#define LOS_ERRNO_QUEUE_GEN_SID_OBJ         LOS_ERRNO_OS_ERROR(LOS_MOD_QUE, 0x32)
#define LOS_ERRNO_QUEUE_CALLED_INTERR       LOS_ERRNO_OS_ERROR(LOS_MOD_QUE, 0x36)
#define LOS_ERRNO_QUEUE_INFO_SIZE_TOO_SMALL LOS_ERRNO_OS_ERROR(LOS_MOD_QUE, 0x37)

/* refs pri header: message-memory flavor carried in LosQueueCB.queueMemType
 * (BOX-only field). Only the DYNAMIC flavor is heap-freed on delete. */
#define OS_QUEUE_ALLOC_DYNAMIC 0
#define OS_QUEUE_ALLOC_STATIC  1

/* refs include/los_queue.h: queue header with a flexible wait-task tail. */
typedef struct tagQueueHeaderInfo {
    UINT32 uwQueueID;   /**< pool index of the queue CB */
    UINT16 usQueueLen;  /**< Queue length */
    UINT16 usQueueSize; /**< Node size */
    UINT16 usQueueHead; /**< Node head */
    UINT16 usQueueTail; /**< Node tail */
    UINT16 usWritableCnt; /**< Count of writable resources */
    UINT16 usReadableCnt; /**< Count of readable resources */
    UINT32 usWaitTask[];  /**< wait bitmap: READ/WRITE/MEM segments */
} QUEUE_HEAD_INFO_S;

/* Wait-task bitmap (refs base/include/los_queue_pri.h verbatim): three
 * consecutive segments, each ceil(taskNum/32) UINT32 words; the bit for a
 * task is (1 << (taskId & 31)) in word (taskId >> 5) of its segment. */
#define QUEUE_WAIT_READ                      0
#define QUEUE_WAIT_WRITE                     1
#define QUEUE_WAIT_MEM                       2
#define QUEUE_WAIT_MAX                       3
#define GET_TASKNUM_MASK_U32(taskNum)        ((((taskNum) - 1) >> 5) + 1)
#define GET_TYPE_IDX(type, taskNum)          ((type) * GET_TASKNUM_MASK_U32(taskNum))
#define GET_READ_ARRAY_IDX(taskId, taskNum)  (((taskId) >> 5) + GET_TYPE_IDX(QUEUE_WAIT_READ, taskNum))
#define GET_WRITE_ARRAY_IDX(taskId, taskNum) (((taskId) >> 5) + GET_TYPE_IDX(QUEUE_WAIT_WRITE, taskNum))
#define GET_MEM_ARRAY_IDX(taskId, taskNum)   (((taskId) >> 5) + GET_TYPE_IDX(QUEUE_WAIT_MEM, taskNum))
#define GET_ARRAY_BIT(taskId)                (1ULL << ((taskId) & 31))
#define QUEUE_WAIT_ARRAY_LEN(taskNum)        (((taskNum) == 0) ? 0 : (((((taskNum) - 1) >> 5) + 1) * QUEUE_WAIT_MAX))
#define QUEUE_WAIT_ALL_SIZE(taskNum)         ((QUEUE_WAIT_ARRAY_LEN(taskNum)) * sizeof(UINT32))
#define QUEUE_ALL_INFO_SIZE(taskNum)         (QUEUE_WAIT_ALL_SIZE(taskNum) + sizeof(QUEUE_HEAD_INFO_S))

/* kernel/box/los_queue_box.c: create bound to a pre-generated sId; export
 * the queue header plus its wait-task bitmap. */
extern UINT32 LOS_QueueCreateWithSId(UINT16 len, UINT32 sId, UINT32 flags,
                                     UINT16 maxMsgSize, UINT8 *queueMem, UINT16 memSize);
extern UINT32 LOS_QueueInfoAllGet(UINT32 sId, UINT32 taskNum,
                                  QUEUE_HEAD_INFO_S *queueInfo, UINT32 length);
#endif
#endif
