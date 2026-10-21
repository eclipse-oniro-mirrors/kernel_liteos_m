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

/* Page-table pool manager (TABLE_BSS_SECTION is empty: the pool lives in
 * .bss, zeroed by reset_vector.S before the MMU bring-up and re-cleared by
 * PageTableInitManager()). */

#include "arch/mmu.h"
#include "arch/mmu_pri.h"
#include "pagetable_manager.h"

#define TABLE_ADDR_ALIGNED __attribute__((aligned(LN_TABLE_ENTRY_NUM * sizeof(UINTPTR))))

STATIC TABLE_BSS_SECTION TABLE_ADDR_ALIGNED UINTPTR g_pageTables[LOSCFG_ARCH_MMU_PAGE_TBL_NUM * LN_TABLE_ENTRY_NUM];
STATIC TABLE_BSS_SECTION INT32 g_pteUsedCnt[LOSCFG_ARCH_MMU_PAGE_TBL_NUM] = {0};

UINT32 PageTableGetIdx(UINTPTR *pte)
{
    UINT32 tblIdx = ((UINTPTR)pte - (UINTPTR)g_pageTables) / LOSCFG_ARCH_ARM64_PAGE_SIZE;

    return tblIdx;
}

/* Modifying page table entry used count, which is no more than LN_TABLE_ENTRY_NUM. */
INT32 PageTableModifyEntryUsedCnt(UINT32 tblIdx, INT32 offset)
{
    if (offset != 0) {
        g_pteUsedCnt[tblIdx] += offset;
    }
    return g_pteUsedCnt[tblIdx];
}

STATIC INLINE VOID SetPageTableFree(UINT32 tblIdx)
{
    g_pteUsedCnt[tblIdx] = PAGE_TABLE_FREE;
}

STATIC INLINE VOID SetPageTableReady(UINT32 tblIdx)
{
    g_pteUsedCnt[tblIdx] = PAGE_TABLE_READY;
}

UINTPTR *PageTableAlloc(VOID)
{
    UINT32 i;
    UINTPTR *table = NULL;

    for (i = 0; i < LOSCFG_ARCH_MMU_PAGE_TBL_NUM; i++) {
        if (PageTableIsFree(i)) {
            table = &g_pageTables[i * LN_TABLE_ENTRY_NUM];
            MMU_LOG(LOS_INFO_LEVEL, "Allocating new page table index[%u], table addr:0x%lx\n", i, (UINTPTR)table);
            SetPageTableReady(i);
            return table;
        }
    }

    MMU_LOG(LOS_ERR_LEVEL, "No enough page tables space left.\n");
    return NULL;
}

VOID PageTableFree(UINTPTR *table)
{
    UINT32 i = PageTableGetIdx(table);

    MMU_LOG(LOS_INFO_LEVEL, "Recycling page table index[%u], pte addr:0x%lx, table addr:0x%lx\n", i,
        (UINTPTR)table, ALIGN_DOWN((UINTPTR)table, LOSCFG_ARCH_ARM64_PAGE_SIZE));
    SetPageTableFree(i);
}

VOID PageTableInitManager(VOID)
{
    UINT32 i;
    PageTableMemInit((UINTPTR)g_pageTables, sizeof(g_pageTables));
    for (i = 0; i < LOSCFG_ARCH_MMU_PAGE_TBL_NUM; i++) {
        SetPageTableFree(i);
    }
    MMU_LOG(LOS_INFO_LEVEL, "Page tables overview: total tables num %d, from 0x%lx to 0x%lx\n",
        LOSCFG_ARCH_MMU_PAGE_TBL_NUM, (UINTPTR)g_pageTables, (UINTPTR)g_pageTables + sizeof(g_pageTables));
}
