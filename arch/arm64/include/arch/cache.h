/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* AArch64 cache maintenance API (implemented in arch/arm64/los_cache.S).
 * The naming follows the liteos_m arch convention shared with the other
 * arches (see arch/risc-v/riscv32/gcc/include/arch/cache.h). */

#ifndef _ARCH_CACHE_H
#define _ARCH_CACHE_H

#include "los_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Range operations: [start, end) virtual addresses. */
extern VOID ArchDCacheInvByAddr(UINTPTR start, UINTPTR end);
extern VOID ArchDCacheCleanByAddr(UINTPTR start, UINTPTR end);
extern VOID ArchDCacheFlushByAddr(UINTPTR start, UINTPTR end);
extern VOID ArchICacheFlushByAddr(UINTPTR start, UINTPTR end);

/* Whole-cache set/way operations. */
extern VOID ArchDCacheInvalidate(VOID);
extern VOID ArchDCacheClean(VOID);
extern VOID ArchDCacheFlush(VOID);
extern VOID ArchDCacheFlushAll(VOID);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ARCH_CACHE_H */
