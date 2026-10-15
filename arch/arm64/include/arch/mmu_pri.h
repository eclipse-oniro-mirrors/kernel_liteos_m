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
 * AArch64 MMU private definitions.
 * Adaptation notes:
 *   - The Kconfig knobs (LOSCFG_ARCH_ARM64_VA_BITS/PA_BITS/PAGE_SHIFT,
 *     LOSCFG_ARCH_MMU_PAGE_TBL_NUM) get #ifndef defaults below: the RK3568 AMP
 *     slave runs a 32-bit VA / 32-bit PA identity map (first table level is
 *     therefore L1), 4K granule, and a 16-entry (64KB) page-table pool.
 *   - MMU_TCR_EL1_CONFIG sets EPD1 (TTBR1 walks disabled):
 *     TTBR1_EL1 is never initialized on this slave and could hold a stale
 *     value from the previous world.
 */

#ifndef _ARCH_MMU_PRI_H
#define _ARCH_MMU_PRI_H

#include "arch/mmu.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef LOSCFG_ARCH_ARM64_VA_BITS
#define LOSCFG_ARCH_ARM64_VA_BITS  32
#endif
#ifndef LOSCFG_ARCH_ARM64_PA_BITS
#define LOSCFG_ARCH_ARM64_PA_BITS  32
#endif
#ifndef LOSCFG_ARCH_ARM64_PAGE_SHIFT
#define LOSCFG_ARCH_ARM64_PAGE_SHIFT 12
#endif
#ifndef LOSCFG_ARCH_ARM64_PAGE_SIZE
#define LOSCFG_ARCH_ARM64_PAGE_SIZE (1U << LOSCFG_ARCH_ARM64_PAGE_SHIFT)
#endif
#ifndef LOSCFG_ARCH_MMU_PAGE_TBL_NUM
#define LOSCFG_ARCH_MMU_PAGE_TBL_NUM 16
#endif

#define ALIGN_UP(x, a)          (((x) + (a) - 1) & ~((UINT64)(a) - 1))
#define ALIGN_DOWN(x, a)        ((x) & ~((UINT64)(a) - 1))

#define BM(base, count, val)      (((UINT64)(val) & ((1ULL << (count)) - 1)) << (base))
#define BITS_32_MASK(high, low)   (((~0UL) << (low)) & ((~0UL) >> (32 - 1 - (high))))
#define BITS_64_MASK(high, low)   (((~0ULL) << (low)) & ((~0ULL) >> (64 - 1 - (high))))

/*********** Block/Page attrs ***********/
/* L0/L1/L2/L3 descriptor types */
#define MMU_PTE_DESC_TYPE_MASK        BM(0, 2, 3)
#define MMU_PTE_DESCRIPTOR_INVALID    BM(0, 2, 0)
#define MMU_PTE_L012_DESCRIPTOR_BLOCK BM(0, 2, 1)
#define MMU_PTE_L012_DESCRIPTOR_TABLE BM(0, 2, 3)
#define MMU_PTE_L3_DESCRIPTOR_PAGE    BM(0, 2, 3)

/* Upper and lower attributes */
#define MMU_PTE_ATTR_MASK                 (MMU_PTE_ATTR_UPPER_MASK | MMU_PTE_ATTR_LOWER_MASK)
#define MMU_PTE_ATTR_UPPER_MASK           BITS_64_MASK(63, 51)
#define MMU_PTE_ATTR_RES_SOFTWARE         BM(55, 4, 0xf)
#define MMU_PTE_ATTR_UXN                  BM(54, 1, 1)
#define MMU_PTE_ATTR_PXN                  BM(53, 1, 1)
#define MMU_PTE_ATTR_CONTIGUOUS           BM(52, 1, 1)

#define MMU_PTE_ATTR_LOWER_MASK           BITS_64_MASK(11, 2)
#define MMU_PTE_ATTR_NON_GLOBAL           BM(11, 1, 1)
#define MMU_PTE_ATTR_AF                   BM(10, 1, 1)
#define MMU_PTE_ATTR_SH_NON_SHAREABLE     BM(8, 2, 0)
#define MMU_PTE_ATTR_SH_OUTER_SHAREABLE   BM(8, 2, 2)
#define MMU_PTE_ATTR_SH_INNER_SHAREABLE   BM(8, 2, 3)

#define MMU_PTE_ATTR_AP_ELn               BM(6, 1, 1)   /* (1ULL << 6) */
#define MMU_PTE_ATTR_AP_EL123             BM(6, 1, 0)   /* (0ULL << 6) */
#define MMU_PTE_ATTR_AP_RO                BM(7, 1, 1)   /* (1ULL << 7) */
#define MMU_PTE_ATTR_AP_RW                BM(7, 1, 0)   /* (0ULL << 7) */
#define MMU_PTE_ATTR_AP_P_RW_U_NA         BM(6, 2, 0)
#define MMU_PTE_ATTR_AP_P_RW_U_RW         BM(6, 2, 1)
#define MMU_PTE_ATTR_AP_P_RO_U_NA         BM(6, 2, 2)
#define MMU_PTE_ATTR_AP_P_RO_U_RO         BM(6, 2, 3)
#define MMU_PTE_ATTR_AP_MASK              BM(6, 2, 3)
#define MMU_PTE_ATTR_NON_SECURE           BM(5, 1, 1)

#define MMU_PTE_ATTR_MAIR_INDEX(attrindex)       BM(2, 3, attrindex)
#define MMU_PTE_ATTR_MAIR_INDEX_MASK             MMU_PTE_ATTR_MAIR_INDEX(7)
#define MMU_PTE_ATTR_DEVICE_nGnRnE               MMU_PTE_ATTR_MAIR_INDEX(MMU_MAIR_DEVICE_nGnRnE_IDX)
#define MMU_PTE_ATTR_DEVICE_nGnRE                MMU_PTE_ATTR_MAIR_INDEX(MMU_MAIR_DEVICE_nGnRE_IDX)
#define MMU_PTE_ATTR_DEVICE_WB                   MMU_PTE_ATTR_MAIR_INDEX(MMU_MAIR_NORMAL_WB_IDX)
#define MMU_PTE_ATTR_DEVICE_NC                   MMU_PTE_ATTR_MAIR_INDEX(MMU_MAIR_NORMAL_NC_IDX)

/*********** MAIR_ELn register bit definitions ***********/
#define MMU_MAIR_IDX_MASK                 MMU_FLAGS_MEM_TYPE_MASK
#define MMU_MAIR_ATTR(index, attr)        BM((index) * 8, 8, (attr))
/* Device-nGnRnE memory */
#define MMU_MAIR_DEVICE_nGnRnE_IDX        MMU_FLAGS_DEVICE_STRONGLY_ORDERED
#define MMU_MAIR_DEVICE_nGnRnE            0x00
/* Device-nGnRE memory */
#define MMU_MAIR_DEVICE_nGnRE_IDX         MMU_FLAGS_DEVICE_EARLY
#define MMU_MAIR_DEVICE_nGnRE             0x04
/* Normal Memory, Outer and Inner are both Write-back, Non-transient, and Read/Write-allocate cache policy */
#define MMU_MAIR_NORMAL_WB_IDX            MMU_FLAGS_NORMAL_WB
#define MMU_MAIR_NORMAL_WB                0xff
/* Normal Memory, Outer and Inner are both Non-cacheable */
#define MMU_MAIR_NORMAL_NC_IDX            MMU_FLAGS_NORMAL_NC
#define MMU_MAIR_NORMAL_NC                0x44
/* Normal Memory, Outer and Inner are both Write-through, Non-transient, and Read/Write-allocate cache policy */
#define MMU_MAIR_NORMAL_WT_IDX            MMU_FLAGS_NORMAL_WT
#define MMU_MAIR_NORMAL_WT                0xbb

#define MMU_MEMORY_TYPES_ATTRS     (MMU_MAIR_ATTR(MMU_MAIR_DEVICE_nGnRnE_IDX, MMU_MAIR_DEVICE_nGnRnE) | \
                                    MMU_MAIR_ATTR(MMU_MAIR_DEVICE_nGnRE_IDX, MMU_MAIR_DEVICE_nGnRE)   | \
                                    MMU_MAIR_ATTR(MMU_MAIR_NORMAL_WB_IDX, MMU_MAIR_NORMAL_WB)         | \
                                    MMU_MAIR_ATTR(MMU_MAIR_NORMAL_NC_IDX, MMU_MAIR_NORMAL_NC))

/*********** TCR_ELn register bit definitions ***********/
#if (LOSCFG_ARCH_ARM64_PA_BITS == 48)
#define MMU_TCR_IPS_VAL 0x5ULL
#elif (LOSCFG_ARCH_ARM64_PA_BITS == 44)
#define MMU_TCR_IPS_VAL 0x4ULL
#elif (LOSCFG_ARCH_ARM64_PA_BITS == 42)
#define MMU_TCR_IPS_VAL 0x3ULL
#elif (LOSCFG_ARCH_ARM64_PA_BITS == 40)
#define MMU_TCR_IPS_VAL 0x2ULL
#elif (LOSCFG_ARCH_ARM64_PA_BITS == 36)
#define MMU_TCR_IPS_VAL 0x1ULL
#elif (LOSCFG_ARCH_ARM64_PA_BITS == 32)
#define MMU_TCR_IPS_VAL 0x0ULL
#endif

#ifdef LOSCFG_ARCH_SECURE_MONITOR_MODE
#define MMU_TCR_INTER_PHY_SIZE      (MMU_TCR_IPS_VAL << 16) /* Bits 18 - 16 */
#else
#define MMU_TCR_TBI1_IGNORE         (0x1ULL << 38)          /* Bit 38 */
#define MMU_TCR_TBI0_IGNORE         (0x1ULL << 37)          /* Bit 37 */
#define MMU_TCR_ASID_8_BITS         (0x0ULL << 36)          /* Bit 36 */
#define MMU_TCR_ASID_16_BITS        (0x1ULL << 36)
#define MMU_TCR_INTER_PHY_SIZE      (MMU_TCR_IPS_VAL << 32) /* Bits 34 - 32 */

#define MMU_TCR_TG1_GRANULE_4K      (2ULL << 30)            /* Bits 31 - 30 */
#define MMU_TCR_TG1_GRANULE_16K     (1ULL << 30)
#define MMU_TCR_TG1_GRANULE_64K     (3ULL << 30)
#define MMU_TCR_SH1_NON             (0ULL << 28)            /* Bits 29 - 28 */
#define MMU_TCR_SH1_OUTER           (2ULL << 28)
#define MMU_TCR_SH1_INNER           (3ULL << 28)
#define MMU_TCR_ORGN1_NC            (0ULL << 26)            /* Bits 27 - 26 */
#define MMU_TCR_ORGN1_WB_RA_WA      (1ULL << 26)
#define MMU_TCR_ORGN1_WT_RA_NWA     (2ULL << 26)
#define MMU_TCR_ORGN1_WB_RA_NWA     (3ULL << 26)
#define MMU_TCR_ORGN1_MASK          (3ULL << 26)
#define MMU_TCR_IRGN1_NC            (0ULL << 24)            /* Bits 25 - 24 */
#define MMU_TCR_IRGN1_WB_RA_WA      (1ULL << 24)
#define MMU_TCR_IRGN1_WT_RA_NWA     (2ULL << 24)
#define MMU_TCR_IRGN1_WB_RA_NWA     (3ULL << 24)
#define MMU_TCR_IRGN1_MASK          (3ULL << 24)
#define MMU_TCR_TTBR1_EL1_DISABLE   (1ULL << 23)            /* Bit 23 */
#define MMU_TCR_TTBR1_EL1_ENABLE    (0ULL << 23)
#define MMU_TCR_T1SZ(x)             ((64ULL - (x)) << 16)   /* Bits 21 - 16 */

#define MMU_TCR_TG0_GRANULE_4K      (0ULL << 14)            /* Bits 15 - 14 */
#define MMU_TCR_TG0_GRANULE_16K     (2ULL << 14)
#define MMU_TCR_TG0_GRANULE_64K     (1ULL << 14)
#define MMU_TCR_SH0_NON             (0ULL << 12)            /* Bits 13 - 12 */
#define MMU_TCR_SH0_OUTER           (2ULL << 12)
#define MMU_TCR_SH0_INNER           (3ULL << 12)
#define MMU_TCR_ORGN0_NC            (0ULL << 10)            /* Bits 11 - 10 */
#define MMU_TCR_ORGN0_WB_RA_WA      (1ULL << 10)
#define MMU_TCR_ORGN0_WT_RA_NWA     (2ULL << 10)
#define MMU_TCR_ORGN0_WB_RA_NWA     (3ULL << 10)
#define MMU_TCR_ORGN0_MASK          (3ULL << 10)
#define MMU_TCR_IRGN0_NC            (0ULL << 8)             /* Bits 9 - 8 */
#define MMU_TCR_IRGN0_WB_RA_WA      (1ULL << 8)
#define MMU_TCR_IRGN0_WT_RA_NWA     (2ULL << 8)
#define MMU_TCR_IRGN0_WB_RA_NWA     (3ULL << 8)
#define MMU_TCR_IRGN0_MASK          (3ULL << 8)
#define MMU_TCR_TTBR0_EL1_DISABLE   (1ULL << 7)             /* Bit 7 */
#define MMU_TCR_TTBR0_EL1_ENBALE    (0ULL << 7)
#define MMU_TCR_T0SZ(x)             ((64ULL - (x)) << 0)    /* Bits 5 - 0 */
#endif

#ifdef LOSCFG_ARCH_SECURE_MONITOR_MODE
#define MMU_TCR_EL3_CONFIG (MMU_TCR_INTER_PHY_SIZE | MMU_TCR_TG0_GRANULE_4K | MMU_TCR_SH0_INNER | \
                            MMU_TCR_ORGN0_WB_RA_WA | MMU_TCR_IRGN0_WB_RA_WA | MMU_TCR_T0SZ(LOSCFG_ARCH_ARM64_VA_BITS))
#endif

#define MMU_VA_MEM_REGION  ((1ULL << LOSCFG_ARCH_ARM64_VA_BITS) - 1)
#ifdef LOSCFG_ARCH_MMU_KERNEL_SPACE
#define MMU_TCR_EL1_CONFIG (MMU_TCR_TBI1_IGNORE | MMU_TCR_ASID_16_BITS | MMU_TCR_INTER_PHY_SIZE | \
                            MMU_TCR_TG1_GRANULE_4K | MMU_TCR_TG0_GRANULE_4K | MMU_TCR_SH1_INNER | \
                            MMU_TCR_ORGN1_WB_RA_WA | MMU_TCR_IRGN1_WB_RA_WA | MMU_TCR_T1SZ(LOSCFG_ARCH_ARM64_VA_BITS))
#define MMU_VA_MEM_OFFSET  (~(MMU_VA_MEM_REGION))
#else
/* EPD1: TTBR1_EL1 is never initialized on the slave core and may hold a
 * stale value from the previous world. */
#define MMU_TCR_EL1_CONFIG (MMU_TCR_TBI1_IGNORE | MMU_TCR_ASID_16_BITS | MMU_TCR_INTER_PHY_SIZE | \
                            MMU_TCR_TG1_GRANULE_4K | MMU_TCR_TG0_GRANULE_4K | MMU_TCR_TTBR1_EL1_DISABLE | \
                            MMU_TCR_SH0_INNER | \
                            MMU_TCR_ORGN0_WB_RA_WA | MMU_TCR_IRGN0_WB_RA_WA | MMU_TCR_T0SZ(LOSCFG_ARCH_ARM64_VA_BITS))
#define MMU_VA_MEM_OFFSET  0x0
#endif

/* Maximum number of Ln page table virtual address bits, 12 for 4K-page */
#define LN_TABLE_VA_BITS_NUM    (LOSCFG_ARCH_ARM64_PAGE_SHIFT - 3)  /* 3: 2^3 = 8B, descriptor size */
/* Maximum number of Ln table entries */
#define LN_TABLE_ENTRIES_SHIFT  LN_TABLE_VA_BITS_NUM
#define LN_TABLE_ENTRY_NUM      (1U << LN_TABLE_ENTRIES_SHIFT)

/*
 * Depending on the size of the possible VA range, there can be even fewer levels. With a 4KB
 * granule, for example, if the TTBCR is set so that low addresses span only 1GB, then levels 0
 * and 1 are not required and the translation starts at level 2, going down to level 3 for 4KB
 * pages. The Last level page table index is always 3, while the first may not be 0.
 */
/* Maximum number of table levels */
#define MAX_PAGE_TABLE_LEVELS            4U
#define LEVELS_TO_VA_BITS(levels)        (LN_TABLE_VA_BITS_NUM * (levels) + LOSCFG_ARCH_ARM64_PAGE_SHIFT)
#define MMU_TRANS_START_LVL_GET(bitNum)  ((LEVELS_TO_VA_BITS(MAX_PAGE_TABLE_LEVELS) - (bitNum)) / LN_TABLE_VA_BITS_NUM)
/* Page table level index: L0 ~ L3 */
#define FIRST_TABLE_LEVEL_INDEX          MMU_TRANS_START_LVL_GET(LOSCFG_ARCH_ARM64_VA_BITS)
#define LAST_TABLE_LEVEL_INDEX           (MAX_PAGE_TABLE_LEVELS - 1)
/* Ln pgae table starting bits shift */
#define LN_TABLE_BITS_SHIFT(level)       LEVELS_TO_VA_BITS(LAST_TABLE_LEVEL_INDEX - (level))
/* Ln pgae table entry index value */
#define LN_TABLE_ENTRY_INDEX(va, level)  (((va) >> LN_TABLE_BITS_SHIFT(level)) & (LN_TABLE_ENTRY_NUM - 1))

extern VOID MmuEnable(VOID);
extern UINT64 MmuDecodeFlags(UINT32 osFlags);
extern UINTPTR OsCachedRemap(UINTPTR physAddr, size_t size);
extern UINTPTR OsNoCachedRemap(UINTPTR physAddr, size_t size);

#define NEXT_TABLE_ADDR_HI_BIT 47
STATIC INLINE UINT64 MmuGetPAddrFromPte(UINT64 pte)
{
    return (pte & BITS_64_MASK(NEXT_TABLE_ADDR_HI_BIT, LOSCFG_ARCH_ARM64_PAGE_SHIFT));
}

STATIC INLINE UINT64 *MmuGetNextTableAddr(UINT64 pte)
{
    return (UINT64 *)(UINTPTR)MmuGetPAddrFromPte(pte);
}

STATIC INLINE VOID MmuInvalidLocalTLB(VOID)
{
#ifdef LOSCFG_ARCH_SECURE_MONITOR_MODE
    __asm__ volatile("dsb sy; tlbi alle3; dsb sy; isb": : : "memory");
#else
    __asm__ volatile("dsb sy; tlbi vmalle1; dsb sy; isb": : : "memory");
#endif
}

STATIC INLINE VOID MmuInvalidGlobalTLB(VOID)
{
#ifdef LOSCFG_ARCH_SECURE_MONITOR_MODE
    __asm__ volatile("dsb ish; tlbi alle3is; dsb ish; isb": : : "memory");
#else
    __asm__ volatile("dsb ish; tlbi vmalle1is; dsb ish; isb": : : "memory");
#endif
}

#ifndef MMU_LOG
extern BOOL MmuLogLevelIsOk(UINT32 level);
#define MMU_LOG(level, format, ...)                 \
do {                                                \
    if (MmuLogLevelIsOk(level)) {                   \
            PRINTK(format, ##__VA_ARGS__);          \
        }                                           \
    } while (0)
#endif /* MMU_LOG */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ARCH_MMU_PRI_H */
