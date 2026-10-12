/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * SPDX-License-Identifier: MulanPSL-2.0
 */

/*
 * @file    liteos/securec.h
 * @brief   LiteOS-M securec backend for the libmica platform layer.
 *
 * The LiteOS-M image links third_party/bounds_checking_function (memcpy_s /
 * memset_s / ...), so the mica_memset_s/mica_memcpy_s shims simply forward to
 * those implementations.
 */

#ifndef __MICA_SECUREC__H__
#error "Include mica/platform/securec.h instead of mica/platform/system/liteos/securec.h"
#endif

#ifndef __MICA_LITEOS_SECUREC__H__
#define __MICA_LITEOS_SECUREC__H__

#include <stddef.h>
/* <securec.h> uses the FILE type in its fscanf_s/fprintf_s declarations
 * without including stdio.h itself: pull it in first (the compat shim for
 * the freestanding build provides FILE). */
#include <stdio.h>
/* angle brackets on purpose: this backend file is itself named securec.h,
 * a quoted include would resolve to this very file instead */
#include <securec.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int mica_memset_s(void *dest, size_t destMax, int c, size_t len)
{
    return memset_s(dest, destMax, c, len);
}

static inline int mica_memcpy_s(void *dest, size_t destMax, const void *src, size_t len)
{
    return memcpy_s(dest, destMax, src, len);
}

#ifdef __cplusplus
}
#endif

#endif /* __MICA_LITEOS_SECUREC__H__ */
