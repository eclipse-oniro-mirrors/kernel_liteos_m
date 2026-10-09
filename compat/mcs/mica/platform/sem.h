/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * SPDX-License-Identifier: MulanPSL-2.0
 */

/*
 * @file    liteos/sem.h
 * @brief   LiteOS-M semaphore backend for the libmica platform layer.
 *
 * mica_sem_t is a LiteOS-M semaphore id (LOS_SemCreate handle). Semaphores
 * are posted from the cross-core IPI handler (interrupt context) and waited
 * on from mica service tasks, which is the supported LiteOS-M usage.
 */

#ifndef __MICA_SEM__H__
#error "Include mica/platform/sem.h instead of mica/platform/system/liteos/sem.h"
#endif

#ifndef __MICA_LITEOS_SEM__H__
#define __MICA_LITEOS_SEM__H__

#include "los_sem.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef UINT32 mica_sem_t;

static inline int mica_sem_init(mica_sem_t *sem, unsigned int value)
{
    return (LOS_SemCreate((UINT16)value, sem) == LOS_OK) ? 0 : -1;
}

static inline void mica_sem_destroy(mica_sem_t sem)
{
    (VOID)LOS_SemDelete(sem);
}

static inline unsigned int mica_sem_post(mica_sem_t sem)
{
    return (LOS_SemPost(sem) == LOS_OK) ? 0 : (unsigned int)-1;
}

static inline unsigned int mica_sem_wait(mica_sem_t sem)
{
    return (LOS_SemPend(sem, LOS_WAIT_FOREVER) == LOS_OK) ? 0 : (unsigned int)-1;
}

#ifdef __cplusplus
}
#endif

#endif /* __MICA_LITEOS_SEM__H__ */
