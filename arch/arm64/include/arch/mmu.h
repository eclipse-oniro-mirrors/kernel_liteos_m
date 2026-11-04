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

/*
 * Generic MMU framework API (region-driven dynamic page tables).
 * Adaptation notes:
 *   - TABLE_BSS_SECTION is empty: the page-table pool lives in plain .bss
 *     (the board reset_vector.S zeroes .bss before the MMU bring-up, and
 *     PageTableInitManager() clears the pool again).
 *   - ArchMmuDefaultRegionReg() is WEAK-empty: every region is registered
 *     by the board (ArchMmuAppRegionReg()).
 * MMU_VA_MEM_REGION / MMU_VA_MEM_OFFSET come from arch/mmu_pri.h.
 */

#ifndef _ARCH_MMU_H
#define _ARCH_MMU_H

#include "los_typedef.h"
#include "los_toolchain.h"
#include "los_error.h"
#include "los_printf.h"
#include "los_debug.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define LOS_ERRNO_MMU_INPUT_OVERFLOW        LOS_ERRNO_OS_ERROR(LOS_MOD_MMU, 0x00)
#define LOS_ERRNO_MMU_INPUT_ZERO            LOS_ERRNO_OS_ERROR(LOS_MOD_MMU, 0x01)
#define LOS_ERRNO_MMU_INPUT_FLAG_INV        LOS_ERRNO_OS_ERROR(LOS_MOD_MMU, 0x02)
#define LOS_ERRNO_MMU_PAGETABLE_OOM         LOS_ERRNO_OS_FATAL(LOS_MOD_MMU, 0x03)
#define LOS_ERRNO_MMU_PTE_BUSY              LOS_ERRNO_OS_FATAL(LOS_MOD_MMU, 0x04)
#define LOS_ERRNO_MMU_REGION_NOT_ENOUGH     LOS_ERRNO_OS_ERROR(LOS_MOD_MMU, 0x05)
#define LOS_ERRNO_MMU_REGION_OVERLAPPED     LOS_ERRNO_OS_ERROR(LOS_MOD_MMU, 0x06)
#define LOS_ERRNO_MMU_INPUT_NULL            LOS_ERRNO_OS_ERROR(LOS_MOD_MMU, 0x07)
#define LOS_ERRNO_MMU_INPUT_ADDR_INV        LOS_ERRNO_OS_ERROR(LOS_MOD_MMU, 0x08)

/*
 * MAIR register support up to 8 types of mem attrs, so we reserve bit[0:2] here
 */
#define MMU_FLAGS_MEM_TYPE_MASK              7U         /* bit[0:2] */
#define MMU_FLAGS_MEM_TYPE(attr)             ((attr) & MMU_FLAGS_MEM_TYPE_MASK)
#define MMU_FLAGS_DEVICE_STRONGLY_ORDERED    0U
#define MMU_FLAGS_DEVICE_EARLY               1U
#define MMU_FLAGS_NORMAL_WB                  2U
#define MMU_FLAGS_NORMAL_NC                  3U
#define MMU_FLAGS_NORMAL_WT                  4U

#define MMU_FLAGS_RO                    0U         /* bit[3] */
#define MMU_FLAGS_RW                    (1U << 3)
#define MMU_FLAGS_EXECUTE_ENABLE        0U         /* bit[4] */
#define MMU_FLAGS_EXECUTE_DISABLE       (1U << 4)
#define MMU_FLAGS_SECURE                0U         /* bit[5] */
#define MMU_FLAGS_NON_SECURE            (1U << 5)

#if defined(LOSCFG_ARCH_SECURE_MONITOR_MODE) || defined(LOSCFG_ARCH_SECURE_OS_MODE)
#define MMU_FLAGS_SECURE_DEFAULT_STATE  MMU_FLAGS_SECURE
#else
#define MMU_FLAGS_SECURE_DEFAULT_STATE  MMU_FLAGS_NON_SECURE
#endif

#define MMU_RW_NX_NORMAL_NC                 \
    (MMU_FLAGS_RW | MMU_FLAGS_EXECUTE_DISABLE | MMU_FLAGS_NORMAL_NC | MMU_FLAGS_SECURE_DEFAULT_STATE)
#define MMU_RO_X_NORMAL_WB                  \
    (MMU_FLAGS_RO | MMU_FLAGS_EXECUTE_ENABLE | MMU_FLAGS_NORMAL_WB | MMU_FLAGS_SECURE_DEFAULT_STATE)
#define MMU_RO_NX_NORMAL_WB                 \
    (MMU_FLAGS_RO | MMU_FLAGS_EXECUTE_DISABLE | MMU_FLAGS_NORMAL_WB | MMU_FLAGS_SECURE_DEFAULT_STATE)
#define MMU_RW_NX_NORMAL_WB                 \
    (MMU_FLAGS_RW | MMU_FLAGS_EXECUTE_DISABLE | MMU_FLAGS_NORMAL_WB | MMU_FLAGS_SECURE_DEFAULT_STATE)
#define MMU_RW_NX_DEVICE_MAP                \
    (MMU_FLAGS_RW | MMU_FLAGS_EXECUTE_DISABLE | MMU_FLAGS_DEVICE_EARLY | MMU_FLAGS_SECURE_DEFAULT_STATE)
#define MMU_RW_NX_DEVICE_STRONGLY_ORDERED   \
    (MMU_FLAGS_RW | MMU_FLAGS_EXECUTE_DISABLE | MMU_FLAGS_DEVICE_STRONGLY_ORDERED | MMU_FLAGS_SECURE_DEFAULT_STATE)

/* The page-table pool lives in plain .bss (zeroed by reset_vector.S
 * before LosMmuInit). */
#define TABLE_BSS_SECTION

#ifndef SYS_MEM_SIZE_OFFSET
#define SYS_MEM_SIZE_OFFSET 0
#endif

#define GET_MMU_VALID_ADDR(addr)   (((UINTPTR)(addr)) & (MMU_VA_MEM_REGION))
#define MMU_VA_TO_PA(vaddr)        (GET_MMU_VALID_ADDR((UINTPTR)(vaddr)) - SYS_MEM_SIZE_OFFSET)
#define MMU_PA_TO_VA(paddr)        (((UINTPTR)(paddr) + SYS_MEM_SIZE_OFFSET) | MMU_VA_MEM_OFFSET)

typedef struct {
    const CHAR *name;
    UINTPTR pAddr;      /* Physical address start value */
    UINTPTR vAddr;      /* Virtual address start value */
    UINTPTR size;       /* MMU memory size */
    UINT32 flags;       /* Memory attributes */
} MmuMapRegion;

typedef struct {
    UINT32 pageUsed;
    UINT32 pageTotal;
    UINT32 pteUsed;
    UINT32 pteTotal;
} PageTablesStat;

extern VOID ArchMmuLogLevelCfg(UINT32 level);
extern VOID ArchMmuSwitch(VOID);
extern VOID ArchMmuConfig(VOID);
extern VOID ArchMmuPageTablesCreate(VOID);
extern UINT32 ArchMmuRegionInsert(MmuMapRegion *rgn);
extern UINT32 ArchMmuTableStatistics(PageTablesStat *usage);
extern UINT32 ArchMmuMap(UINTPTR vAddr, UINTPTR pAddr, UINTPTR size, UINT32 attrs);
extern UINT32 ArchMmuUnmap(UINTPTR vAddr, UINTPTR size);
extern UINT32 ArchMmuTableWalk(UINTPTR vAddr, UINTPTR size);

extern VOID ArchMmuAppRegionReg(VOID);

extern UINTPTR *g_firstPageTable;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ARCH_MMU_H */
