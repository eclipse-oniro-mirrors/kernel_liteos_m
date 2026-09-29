/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Minimal freestanding <pthread.h> shim for the LiteOS-M slave core.
 *
 * The libmica service layer spawns its tty/umt/receiver threads through the
 * pthread API. On LiteOS-M those map onto LOS_TaskCreate tasks: the start
 * routine and its argument travel through a small heap descriptor because
 * TSK_INIT_PARAM_S.uwArg is a 32-bit scalar (safe: the whole image, heap
 * included, lives below 4GB on this board). Each thread gets a fixed
 * 8KB stack and priority 20 (below time-critical work, above demo tasks).
 */

#ifndef MICA_COMPAT_PTHREAD_H__
#define MICA_COMPAT_PTHREAD_H__

#include <stddef.h>
#include <stdint.h>
#include "los_config.h"
#include "los_task.h"
#include "los_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef UINT32 pthread_t;
typedef struct {
    unsigned int stacksize;
} pthread_attr_t;

#define PTHREAD_CREATE_DETACHED 1

struct pthread_start_desc {
    void *(*fn)(void *);
    void *arg;
};

static inline UINT32 PthreadEntry(UINT32 arg)
{
    struct pthread_start_desc *desc = (struct pthread_start_desc *)(uintptr_t)arg;
    void *(*fn)(void *) = desc->fn;
    void *fn_arg = desc->arg;

    (VOID)LOS_MemFree((VOID *)LOSCFG_SYS_HEAP_ADDR, desc);
    (VOID)fn(fn_arg);
    /* start routine returned: self-terminate */
    (VOID)LOS_TaskDelete(LOS_CurTaskIDGet());
    return 0;
}

static inline int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                 void *(*start_routine)(void *), void *arg)
{
    struct pthread_start_desc *desc;
    TSK_INIT_PARAM_S taskParam = {0};

    (VOID)attr;
    if (thread == NULL || start_routine == NULL) {
        return -1;
    }

    desc = (struct pthread_start_desc *)LOS_MemAlloc((VOID *)LOSCFG_SYS_HEAP_ADDR,
                                                     sizeof(*desc));
    if (desc == NULL) {
        return -1;
    }
    desc->fn = start_routine;
    desc->arg = arg;

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)PthreadEntry;
    taskParam.uwArg = (UINT32)(uintptr_t)desc;
    taskParam.usTaskPrio = 20;
    taskParam.uwStackSize = 0x2000;
    taskParam.pcName = "mica_thread";
    if (LOS_TaskCreate(thread, &taskParam) != LOS_OK) {
        (VOID)LOS_MemFree((VOID *)LOSCFG_SYS_HEAP_ADDR, desc);
        return -1;
    }
    return 0;
}

static inline void pthread_exit(void *value)
{
    (VOID)value;
    (VOID)LOS_TaskDelete(LOS_CurTaskIDGet());
    /* unreachable on success */
    while (1) {
    }
}

static inline int pthread_attr_init(pthread_attr_t *attr)
{
    if (attr == NULL) {
        return -1;
    }
    attr->stacksize = 0x2000;
    return 0;
}

static inline int pthread_attr_destroy(pthread_attr_t *attr)
{
    (VOID)attr;
    return 0;
}

static inline int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
    (VOID)attr;
    (VOID)detachstate;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* MICA_COMPAT_PTHREAD_H__ */
