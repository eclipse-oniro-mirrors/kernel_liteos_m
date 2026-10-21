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
 * AArch64 MMU hardware configuration: OS-flag to PTE-attribute decoding
 * (MmuDecodeFlags), translation regime bring-up (ArchMmuSwitch /
 * ArchMmuConfig), MMU enable and page-table pool clearing (MmuEnable /
 * PageTableMemInit), and cacheability remap helpers (OsCachedRemap /
 * OsNoCachedRemap). All paths target non-secure EL1
 * (LOSCFG_ARCH_SECURE_MONITOR_MODE is not defined in this tree).
 */

#include "los_typedef.h"
#include "los_toolchain.h"
#include "arch/regs.h"
#include "arch/mmu.h"
#include "arch/mmu_pri.h"
#include "pagetable_manager.h"

/* SYS_MEM_BASE restricts CacheRemap() to system memory; default 0 keeps
 * every physical range remappable (the board does not define one). */
#ifndef SYS_MEM_BASE
#define SYS_MEM_BASE 0
#endif

UINT64 MmuDecodeFlags(UINT32 osFlags)
{
    UINT64 mmuFlags;
    UINT32 memType;

    /* Unprivileged mode: user el0 mode never execute */
#ifdef LOSCFG_ARCH_SECURE_MONITOR_MODE
    mmuFlags = MMU_PTE_ATTR_AF;
#else
    mmuFlags = MMU_PTE_ATTR_AF | MMU_PTE_ATTR_UXN;
#endif

    /*
     * Unprivileged mode: user el0 mode has no access to mems any time
     * Privileged mode: kernel el123 mode has read/write or read-only access
     */
    mmuFlags |= (osFlags & MMU_FLAGS_RW) ? MMU_PTE_ATTR_AP_P_RW_U_NA : MMU_PTE_ATTR_AP_P_RO_U_NA;

    /* Privileged mode: kernel el123 mode optionally execute */
    mmuFlags |= (osFlags & MMU_FLAGS_EXECUTE_DISABLE) ? MMU_PTE_ATTR_PXN : 0;

    /*
     * Non-secure bit:
     * For memory accesses from Secure state, specifies whether the output address is in Secure or Non-secure memory.
     * For memory accesses from Non-secure state, this bit is RES0 and is ignored by the PE.
     */
    mmuFlags |= (osFlags & MMU_FLAGS_NON_SECURE) ? MMU_PTE_ATTR_NON_SECURE : 0;

    memType = osFlags & MMU_FLAGS_MEM_TYPE_MASK;
    mmuFlags |= MMU_PTE_ATTR_MAIR_INDEX(memType);

    /* Config shareability according to mem type */
    switch (memType) {
        case MMU_MAIR_DEVICE_nGnRnE_IDX:
        case MMU_MAIR_DEVICE_nGnRE_IDX:
            mmuFlags |= MMU_PTE_ATTR_SH_OUTER_SHAREABLE;
            break;
        case MMU_MAIR_NORMAL_NC_IDX:
        case MMU_MAIR_NORMAL_WB_IDX:
            mmuFlags |= MMU_PTE_ATTR_SH_INNER_SHAREABLE;
            break;
        default:
            break;
    }

    return mmuFlags;
}

VOID ArchMmuSwitch(VOID)
{
#ifdef LOSCFG_ARCH_SECURE_MONITOR_MODE
    /* Initialize Memory Attribute Indirection Register */
    AARCH64_SYSREG_WRITE(mair_el3, MMU_MEMORY_TYPES_ATTRS);
    /* Initialize TCR_EL3 */
    /* set cacheable attributes on translation walk */
    /* non-shareable, inner write-back write-allocate */
    AARCH64_SYSREG_WRITE(tcr_el3, MMU_TCR_EL3_CONFIG);
    /* Set translation table base addr */
    AARCH64_SYSREG_WRITE(ttbr0_el3, g_firstPageTable);
#else
    /* Initialize Memory Attribute Indirection Register */
    AARCH64_SYSREG_WRITE(mair_el1, MMU_MEMORY_TYPES_ATTRS);
    /* Initialize TCR_EL1 */
    /* set cacheable attributes on translation walk */
    /* non-shareable, inner write-back write-allocate */
    AARCH64_SYSREG_WRITE(tcr_el1, MMU_TCR_EL1_CONFIG);
#ifdef LOSCFG_ARCH_MMU_KERNEL_SPACE
    /* Set translation table base addr */
    AARCH64_SYSREG_WRITE(ttbr1_el1, (UINTPTR*)MMU_VA_TO_PA(g_firstPageTable));
#else
    /* Set translation table base addr */
    AARCH64_SYSREG_WRITE(ttbr0_el1, (UINTPTR*)MMU_VA_TO_PA(g_firstPageTable));
#endif

#endif

    /* All cores process */
    MmuInvalidLocalTLB();
}

VOID ArchMmuConfig(VOID)
{
    ArchMmuSwitch();
    MmuEnable();
}

/* ****************************************************************************
 * MmuEnable: turn the MMU (SCTLR_EL1.M) on with full TLB invalidation
 * around it:
 *      tlbi vmalle1is; isb; dsb sy          (invalidate TLB)
 *      sctlr_el1 |= 0x1; isb                (turn on the MMU)
 *      tlbi vmalle1; isb; dsb sy            (invalidate TLB)
 * Note: only the M (MMU) bit is set - I/D caches are enabled separately
 * by the board (see targets/rk3568_m/liteos_m/arch/arm64/los_mmu.c).
 * **************************************************************************** */
VOID MmuEnable(VOID)
{
    /* Invalidate TLB */
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("isb");
    __asm__ volatile("dsb sy");

    /* Turn on the MMU */
    __asm__ volatile("mrs x9, sctlr_el1\n"
                     "orr x9, x9, #0x1\n"
                     "msr sctlr_el1, x9\n":::"x9", "memory");
    __asm__ volatile("isb");

    /* Invalidate TLB */
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("isb");
    __asm__ volatile("dsb sy");
}

/* ****************************************************************************
 * PageTableMemInit: zero the page-table pool, 8 bytes per iteration.
 * addr: pagetable base, size: byte count.
 * **************************************************************************** */
VOID PageTableMemInit(UINTPTR addr, UINTPTR size)
{
    UINTPTR offset;

    for (offset = 0; offset < size; offset += sizeof(UINTPTR)) {
        *(UINTPTR *)(addr + offset) = 0;
    }
}

STATIC UINTPTR CacheRemap(UINTPTR physAddr, size_t size, UINT32 attrs)
{
#ifndef LOSCFG_ARCH_MMU_VA_MANAGE
    UINT32 ret;
    if (physAddr < SYS_MEM_BASE) {
        return 0;
    }
    ret = ArchMmuUnmap(physAddr, size);
    if (ret != LOS_OK) {
        return 0;
    }
    ret = ArchMmuMap(physAddr, physAddr, size, attrs);
    if (ret != LOS_OK) {
        PRINT_ERR("[%s] failed, ret:0x%x.\n", __FUNCTION__, ret);
        return 0;
    }
    return physAddr;
#else
    (void)physAddr;
    (void)size;
    PRINTK("Warning! OS does not support virtual addresses to be different from physical addresses.\n");
    return 0;
#endif
}

UINTPTR OsCachedRemap(UINTPTR physAddr, size_t size)
{
    return CacheRemap(physAddr, size, MMU_RW_NX_NORMAL_WB);
}

UINTPTR OsNoCachedRemap(UINTPTR physAddr, size_t size)
{
    return CacheRemap(physAddr, size, MMU_RW_NX_NORMAL_NC);
}
