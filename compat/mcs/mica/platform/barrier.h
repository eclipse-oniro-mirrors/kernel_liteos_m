/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * SPDX-License-Identifier: MulanPSL-2.0
 */

/*
 * @file    liteos/barrier.h
 * @brief   LiteOS-M memory barrier backend for the libmica platform layer.
 *
 * On this AMP setup the slave core runs with the MMU off but D-cache on,
 * while the master core writes the resource table (status word in
 * reserved[0]) and the vrings over DDR. The L2 is the point of coherency
 * between the cores; only the slave's private L1D can hold stale lines.
 *
 * mica_mb() therefore performs a clean+invalidate (dc civac) over the
 * resource table region before status is (re)read, and pushes any locally
 * dirty lines (e.g. reserved[0] cleared after a reset) down to L2 so the
 * master observes them. Vring and rpmsg buffer coherence is already handled
 * per-range by open-amp itself (VIRTIO_CACHED_VRINGS + metal_cache_*).
 */

#ifndef __MICA_BARRIER__H__
#error "Include mica/platform/barrier.h instead of mica/platform/system/liteos/barrier.h"
#endif

#ifndef __MICA_LITEOS_BARRIER__H__
#define __MICA_LITEOS_BARRIER__H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* provided by libmica's ped_rsc_table.c */
extern void rsc_table_get(void **table_ptr, int *length);

#define MICA_LITEOS_CACHE_LINE_SZ 64

static inline void mica_mb(void)
{
    void *rsc;
    int len;
    uintptr_t start, end;

    rsc_table_get(&rsc, &len);
    start = (uintptr_t)rsc & ~((uintptr_t)MICA_LITEOS_CACHE_LINE_SZ - 1U);
    end = (uintptr_t)rsc + (uintptr_t)len;

    __asm__ volatile("dsb sy" ::: "memory");
    for (; start < end; start += MICA_LITEOS_CACHE_LINE_SZ) {
        __asm__ volatile("dc civac, %0" : : "r"(start) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

#ifdef __cplusplus
}
#endif

#endif /* __MICA_LITEOS_BARRIER__H__ */
