/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
 *
 * Minimal freestanding <stdlib.h> shim for the LiteOS-M slave core.
 *
 * The LiteOS-M build compiles with -nostdinc: only the clang resource
 * (freestanding) headers are available. libmetal/openamp public headers
 * still include <stdlib.h>, so provide the minimal declarations they
 * reference. Allocation in libmetal is routed through metal/alloc.h
 * (LOS_MemAlloc), so malloc() is never expected to link; declaring it
 * keeps the headers compilable and turns any accidental use into a
 * loud link-time error.
 */

#ifndef MICA_COMPAT_STDLIB_H__
#define MICA_COMPAT_STDLIB_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void free(void *ptr);
void abort(void);

#ifdef __cplusplus
}
#endif

#endif /* MICA_COMPAT_STDLIB_H__ */
