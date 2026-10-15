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

/* Page-table pool manager API. */

#ifndef _PAGETABLE_MANAGER_H
#define _PAGETABLE_MANAGER_H

#include "los_typedef.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern VOID PageTableMemInit(UINTPTR addr, UINTPTR size);

extern VOID PageTableInitManager(VOID);

extern UINTPTR *PageTableAlloc(VOID);
extern VOID PageTableFree(UINTPTR *table);

extern INT32 PageTableModifyEntryUsedCnt(UINT32 tblIdx, INT32 offset);
extern UINT32 PageTableGetIdx(UINTPTR *pte);

#define  PAGE_TABLE_FREE  UINT16_MAX
#define  PAGE_TABLE_READY 0

STATIC INLINE BOOL PageTableIsFree(UINT32 tblIdx)
{
    return PageTableModifyEntryUsedCnt(tblIdx, 0) == PAGE_TABLE_FREE;
}

STATIC INLINE BOOL PageTableIsReady(UINT32 tblIdx)
{
    return PageTableModifyEntryUsedCnt(tblIdx, 0) == PAGE_TABLE_READY;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _PAGETABLE_MANAGER_H */
