/*
 * Minimal shim of the old liteos los_sec_pri.h: only what the staged
 * boxes_cfg / usr_demo sources need to compile. The reduced EntityObj and
 * SIdObj shapes assume LOSCFG_BASE_IPC_PERM / RECOVER / SID_LIST are OFF
 * (they are in this build). M5 restores the full file from refs.
 */
#ifndef LOS_SEC_PRI_H
#define LOS_SEC_PRI_H

#include "los_typedef.h"
#include "los_list.h"
#include "los_singly_list_pri.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOSCFG_SID_NAME_MAX_LEN 16

/* Chain-visibility grab point: boxes_res_conf.h includes this shim
 * before boxes_res_auto.cfg, whose externs need LosTaskStack; the
 * usr_demo sources need LOS_TASK_STACK_ALIGN at file scope. */
#define LOSCFG_USR_THREAD_KERNEL_STACK_SIZE 0x200
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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LOS_SEC_PRI_H */
