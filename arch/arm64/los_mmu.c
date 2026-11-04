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
 * Generic dynamic MMU framework (region-driven page tables). Replaces
 * the previous static identity-map tables on rk3568_m; the board now
 * registers its regions through ArchMmuAppRegionReg() (see
 * targets/rk3568_m/liteos_m/arch/arm64/los_mmu.c) and
 * ArchMmuPageTablesCreate() expands them into the dynamically allocated
 * page-table pool.
 *
 * Design notes (see the header block of include/arch/mmu.h):
 *   - TABLE_BSS_SECTION is empty (pool in plain .bss, zeroed by
 *     reset_vector.S before LosMmuInit).
 *   - ArchMmuDefaultRegionReg() is WEAK-empty: every region comes from
 *     the board's ArchMmuAppRegionReg() instead.
 *   - ArchMmuAppRegionReg() gets a WEAK-empty default so the framework
 *     links standalone; the board overrides it.
 */

#include "arch/mmu.h"
#include "arch/mmu_pri.h"
#include "pagetable_manager.h"
#include "los_spinlock.h"
#include "los_task.h"
#include "los_memory_pri.h"
#include <string.h>

STATIC SPIN_LOCK_INIT(g_mmuSpin);

/* Pointing to first page table, which is usually L0 or L1 page table */
TABLE_BSS_SECTION UINTPTR *g_firstPageTable = NULL;

/* Set pte pointing to next page table */
STATIC INLINE VOID MmuSetPteToNextTbl(UINT64 *pte, UINT64 *table)
{
    /* Caller make sure that pte is not NULL */
    *pte = MMU_PTE_L012_DESCRIPTOR_TABLE | (UINTPTR)table;
}

/* Check pte is pointing to next page table */
STATIC INLINE BOOL MmuPteIsNextTable(UINT64 pte, UINT32 level)
{
    return (level != LAST_TABLE_LEVEL_INDEX) && ((pte & MMU_PTE_DESC_TYPE_MASK) == MMU_PTE_L012_DESCRIPTOR_TABLE);
}

/* Set pte to block or page mapping */
STATIC INLINE VOID MmuSetPteToBlkPage(UINT64 *pte, UINT64 pAddr, UINT64 props, UINT32 level)
{
    /* Caller make sure that pte is not NULL */
    UINT64 tmp = props;
    tmp |= (level == LAST_TABLE_LEVEL_INDEX) ? MMU_PTE_L3_DESCRIPTOR_PAGE : MMU_PTE_L012_DESCRIPTOR_BLOCK;
    *pte = tmp | pAddr;
}

/* Check pte is block or page mapping */
STATIC INLINE BOOL MmuPteIsBlkPage(UINT64 pte)
{
    return (pte & MMU_PTE_DESC_TYPE_MASK) == MMU_PTE_L012_DESCRIPTOR_BLOCK;
}

/* Set pte to invalid */
STATIC INLINE VOID MmuSetPteToInvalid(UINT64 *pte)
{
    /* Caller make sure that pte is not NULL */
    *pte = 0;
}

/* Check pte is invalid */
STATIC INLINE BOOL MmuPteIsInvalid(UINT64 pte)
{
    return (pte & MMU_PTE_DESC_TYPE_MASK) == MMU_PTE_DESCRIPTOR_INVALID;
}

/* Translating cur level page table entry properties to next level */
STATIC INLINE UINT64 MmuGetNextLevelProps(UINT64 desc, UINT32 level)
{
    (VOID)level;
    /*
     * arm32 mmu: block and page descriptor properties format is different, so decoding desc to get props is needed.
     * arm64 mmu: block and page descriptor properties format is same, just assign it directly.
     */
    return desc & MMU_PTE_ATTR_MASK;
}

/***************** Mmu log module *****************/
STATIC UINT32 g_mmuLogLevel = LOS_ERR_LEVEL; /* origine is LOS_ERR_LEVEL */

BOOL MmuLogLevelIsOk(UINT32 level)
{
    if (level <= g_mmuLogLevel) {
        return TRUE;
    }
    return FALSE;
}

VOID ArchMmuLogLevelCfg(UINT32 level)
{
    if (level > LOS_DEBUG_LEVEL) {
        return;
    }
    g_mmuLogLevel = level;
}

STATIC const CHAR *MmuDecodeRwAttr(const UINT64 *pte)
{
    const CHAR *ptr = NULL;

    switch (*pte & MMU_PTE_ATTR_AP_MASK)  {
        case MMU_PTE_ATTR_AP_P_RW_U_NA:
            ptr = "PRW-UNA";
            break;
        case MMU_PTE_ATTR_AP_P_RW_U_RW:
            ptr = "PRW-URW";
            break;
        case MMU_PTE_ATTR_AP_P_RO_U_NA:
            ptr = "PRO-UNA";
            break;
        case MMU_PTE_ATTR_AP_P_RO_U_RO:
            ptr = "PRO-URO";
            break;
        default:
            ptr = "UNDEF";
            break;
    }
    return ptr;
}

STATIC const CHAR *MmuDecodeMemType(const UINT64 *pte)
{
    const CHAR *ptr = NULL;

    switch (*pte & MMU_PTE_ATTR_MAIR_INDEX_MASK)  {
        case MMU_PTE_ATTR_DEVICE_nGnRnE:
            ptr = "Device-nGnRnE";
            break;
        case MMU_PTE_ATTR_DEVICE_nGnRE:
            ptr = "Device-nGnRE";
            break;
        case MMU_PTE_ATTR_DEVICE_WB:
            ptr = "NORMAL-CACHE";
            break;
        case MMU_PTE_ATTR_DEVICE_NC:
            ptr = "NORMAL-NonCACHE";
            break;
        default:
            ptr = "UNDEF";
            break;
    }
    return ptr;
}

STATIC INLINE UINT64 MmuDecodeAddr(UINT64 entry)
{
    return (entry & ~(MMU_PTE_ATTR_MASK | MMU_PTE_DESC_TYPE_MASK));
}

STATIC INLINE VOID MmuShowBlockPageDesc(UINT64 *pte, UINT64 vAddr)
{
    if (MmuPteIsBlkPage(*pte)) {
        MMU_LOG(LOS_DEBUG_LEVEL, "[Block] ");
    } else {
        MMU_LOG(LOS_DEBUG_LEVEL, "[Page] ");
    }
    MMU_LOG(LOS_DEBUG_LEVEL, "[Vaddr:0x%llx->Paddr:0x%llx] ", vAddr, MmuDecodeAddr(*pte));

    MMU_LOG(LOS_DEBUG_LEVEL, "[Attrs:0x%llx %s", *pte & MMU_PTE_ATTR_MASK, MmuDecodeMemType(pte));
    MMU_LOG(LOS_DEBUG_LEVEL, "-%s", MmuDecodeRwAttr(pte));
    if (*pte & MMU_PTE_ATTR_PXN) {
        MMU_LOG(LOS_DEBUG_LEVEL, "-PXN");
    } else {
        MMU_LOG(LOS_DEBUG_LEVEL, "-PX");
    }

    if (*pte & MMU_PTE_ATTR_UXN) {
        MMU_LOG(LOS_DEBUG_LEVEL, "-UXN");
    } else {
        MMU_LOG(LOS_DEBUG_LEVEL, "-UX");
    }
    MMU_LOG(LOS_DEBUG_LEVEL, "]\n");
}

STATIC INLINE VOID MmuShowTableDesc(UINT64 *pte, UINT64 vAddr)
{
    UINT64 *table = MmuGetNextTableAddr(*pte);
    table = (UINTPTR*)MMU_PA_TO_VA(table);
    MMU_LOG(LOS_DEBUG_LEVEL, "[Table] [Tab:0x%llx->NextTab:[%u]0x%lx] [VAddr:0x%llx] [Attrs:0x%llx] \n",
        MmuDecodeAddr((UINTPTR)pte), PageTableGetIdx(table), (UINTPTR)table, vAddr, *pte & MMU_PTE_ATTR_MASK);
}

STATIC VOID MmuPrintPte(UINT64 vAddr, UINT64 *pte, UINT32 level)
{
    UINT32 i = level;

    while (i > FIRST_TABLE_LEVEL_INDEX) {
        MMU_LOG(LOS_DEBUG_LEVEL, "%s", "  ");
        i--;
    }
    MMU_LOG(LOS_DEBUG_LEVEL, "[%u][*0x%lx:0x%llx] [L%u] ", PageTableGetIdx(pte), (UINTPTR)pte, *pte, level);

    if (MmuPteIsInvalid(*pte)) {
        MMU_LOG(LOS_DEBUG_LEVEL, "VAddr:0x%llx invalid\n", vAddr);
        return;
    }

    if (MmuPteIsNextTable(*pte, level)) {
        MmuShowTableDesc(pte, vAddr);
        return;
    }

    MmuShowBlockPageDesc(pte, vAddr);
}

/***************** Mmu region module *****************/
#define MMU_MAP_REGION_NUM 50U
STATIC TABLE_BSS_SECTION struct {
    MmuMapRegion area[MMU_MAP_REGION_NUM];
    UINT32 usedCnt;
} g_regionInfo __attribute__((aligned(0x10)));

STATIC INLINE BOOL MmuRegionIsOverlapped(UINTPTR vAddr1, UINTPTR size1, UINTPTR vAddr2, UINTPTR size2)
{
    if (((vAddr1 + size1) <= vAddr2) || (vAddr1 >= (vAddr2 + size2))) {
        return FALSE;
    }
    return TRUE;
}

STATIC INLINE BOOL MmuAddrIsOverMaxBits(UINTPTR addr, UINTPTR maxBitsNum)
{
    if (addr & ~((1ULL << maxBitsNum) - 1)) {
        return TRUE;
    }
    return FALSE;
}

#define MMU_UINTPTR_MAX    ((UINTPTR)-1)
STATIC INLINE UINT32 MmuCheckAddrParam(UINTPTR vAddr, UINTPTR pAddr, UINTPTR size, UINT32 flag)
{
    UINTPTR bitsNum;

    if (size == 0) {
        return LOS_ERRNO_MMU_INPUT_ZERO;
    }
#ifndef LOSCFG_ARCH_MMU_VA_MANAGE
    if (vAddr != pAddr) {
        MMU_LOG(LOS_ERR_LEVEL, "The vAddr[0x%lx] must be equal to the pAddr[0x%lx].\n", vAddr, pAddr);
        return LOS_ERRNO_MMU_INPUT_ADDR_INV;
    }
#endif

    if ((vAddr > (MMU_UINTPTR_MAX - size)) || (pAddr > (MMU_UINTPTR_MAX - size))) {
        return LOS_ERRNO_MMU_INPUT_OVERFLOW;
    }

    bitsNum = (LOSCFG_ARCH_ARM64_VA_BITS > LOSCFG_ARCH_ARM64_PA_BITS) ?
        LOSCFG_ARCH_ARM64_PA_BITS : LOSCFG_ARCH_ARM64_VA_BITS;

    if (MmuAddrIsOverMaxBits(vAddr + size, bitsNum) ||
        MmuAddrIsOverMaxBits(pAddr + size, bitsNum)) {
        return LOS_ERRNO_MMU_INPUT_OVERFLOW;
    }

    switch (flag) {
        case MMU_RW_NX_NORMAL_NC:
        case MMU_RO_X_NORMAL_WB:
        case MMU_RO_NX_NORMAL_WB:
        case MMU_RW_NX_NORMAL_WB:
        case MMU_RW_NX_DEVICE_MAP:
        case MMU_RW_NX_DEVICE_STRONGLY_ORDERED:
            break;
        default:
            return LOS_ERRNO_MMU_INPUT_FLAG_INV;
    }
    return LOS_OK;
}

UINT32 ArchMmuRegionInsert(MmuMapRegion *rgn)
{
    UINT32 idx;
    UINTPTR alignVAddr = ALIGN_DOWN(rgn->vAddr, LOSCFG_ARCH_ARM64_PAGE_SIZE);
    UINTPTR alignPAddr = ALIGN_DOWN(rgn->pAddr, LOSCFG_ARCH_ARM64_PAGE_SIZE);
    UINTPTR alignSize = ALIGN_UP(rgn->vAddr + rgn->size, LOSCFG_ARCH_ARM64_PAGE_SIZE) - alignVAddr;
    UINT32 ret = MmuCheckAddrParam(alignVAddr, alignPAddr, alignSize, rgn->flags);
    if (ret != LOS_OK) {
        return ret;
    }

    if (g_regionInfo.usedCnt >= MMU_MAP_REGION_NUM) {
        return LOS_ERRNO_MMU_REGION_NOT_ENOUGH;
    }

    for (idx = 0; idx < g_regionInfo.usedCnt; idx++) {
        if (MmuRegionIsOverlapped(alignVAddr, alignSize, g_regionInfo.area[idx].vAddr, g_regionInfo.area[idx].size)) {
            MMU_LOG(LOS_ERR_LEVEL, "Conflict with region:%s.\n", g_regionInfo.area[idx].name);
            return LOS_ERRNO_MMU_REGION_OVERLAPPED;
        }
    }

    g_regionInfo.area[g_regionInfo.usedCnt].name = rgn->name;
    g_regionInfo.area[g_regionInfo.usedCnt].pAddr = alignPAddr;
    g_regionInfo.area[g_regionInfo.usedCnt].vAddr = alignVAddr;
    g_regionInfo.area[g_regionInfo.usedCnt].size = alignSize;
    g_regionInfo.area[g_regionInfo.usedCnt].flags = rgn->flags;
    g_regionInfo.usedCnt++;
    return LOS_OK;
}

/***************** Mmu map/unmap module *****************/
STATIC INLINE VOID MmuPrintPageItems(UINTPTR vAddr, UINTPTR *pte, UINT32 level)
{
    /* Log system active when debug or info level */
    if (MmuLogLevelIsOk(LOS_DEBUG_LEVEL)) {
        MmuPrintPte(vAddr, pte, level);
    }
}

/* Set pte pointing to next page table */
STATIC INLINE VOID MmuSetNextTableAddr(UINTPTR vAddr, UINTPTR *pte, UINTPTR *table, UINT32 level, INT32 cnt)
{
    MmuSetPteToNextTbl(pte, table);
    (VOID)PageTableModifyEntryUsedCnt(PageTableGetIdx(pte), cnt);
    MmuPrintPageItems(vAddr, pte, level);
}

/* Set pte to block(section) or page mapping */
STATIC INLINE VOID MmuSetBlkPageMapping(UINTPTR vAddr, UINTPTR *pte, UINTPTR pAddr, UINTPTR attrs, UINT32 level)
{
    MmuSetPteToBlkPage(pte, pAddr, attrs, level);
    (VOID)PageTableModifyEntryUsedCnt(PageTableGetIdx(pte), 1);
    MmuPrintPageItems(vAddr, pte, level);
}

/* Clear pte to invalid descriptor */
STATIC INLINE VOID MmuClearPteInvalid(UINTPTR vAddr, UINTPTR *pte, UINT32 level)
{
    MmuSetPteToInvalid(pte);
    (VOID)PageTableModifyEntryUsedCnt(PageTableGetIdx(pte), -1);
    MmuPrintPageItems(vAddr, pte, level);
}

/* Copy current level pte info to next table */
STATIC UINTPTR *MmuCopyToNextPte(UINTPTR vAddr, UINTPTR *pte, UINT32 level)
{
    UINTPTR desc = *pte;
    UINTPTR i, step;
    UINTPTR pAddr = MmuGetPAddrFromPte(desc);
    UINTPTR nextLevelProps = MmuGetNextLevelProps(desc, level);
    UINTPTR *table = PageTableAlloc();
    if (table == NULL) {
        return NULL;
    }

    MMU_LOG(LOS_DEBUG_LEVEL, "Copy cur table[%u] [L%u] pte[*0x%lx:0x%016lx] to next table[%u]0x%lx\n",
        PageTableGetIdx(pte), level, (UINTPTR)pte, desc, PageTableGetIdx(table), (UINTPTR)table);

    step = LN_TABLE_BITS_SHIFT(level + 1);
    for (i = 0; i < LN_TABLE_ENTRY_NUM; i++) {
        MmuSetBlkPageMapping(vAddr | (i << step), table + i, pAddr | (i << step), nextLevelProps, level + 1);
    }

    /* Caller make sure that level is L0~L2, instead of L3 */
    MmuSetNextTableAddr(vAddr, pte, (UINTPTR*)MMU_VA_TO_PA(table), level, 0);

    return table;
}

STATIC UINT32 MmuCreateMapNoLock(UINTPTR *ptrTbl, const CHAR *name, UINTPTR pAddr,
    UINTPTR vAddr, UINTPTR size, UINT32 flags)
{
    UINTPTR attrs = MmuDecodeFlags(flags);
    UINTPTR levelSize;
    UINTPTR *pte = NULL;
    UINTPTR *table = ptrTbl;
    UINTPTR pa = pAddr;
    UINTPTR va = vAddr;
    UINTPTR sz = size;
    UINT32 level = FIRST_TABLE_LEVEL_INDEX;
    MMU_LOG(LOS_INFO_LEVEL, "Mapping [%s]: vAddr 0x%lx pAddr 0x%lx size 0x%lx attr 0x%lx\n",
            name, va, pa, sz, attrs);

    while (sz > 0) {
        pte = &table[LN_TABLE_ENTRY_INDEX(va, level)];
        /* Skip next table desc to block/page/invalid mapping */
        if (MmuPteIsNextTable(*pte, level)) {
            level++;
            table = MmuGetNextTableAddr(*pte);
            table = (UINTPTR*)MMU_PA_TO_VA(table);
            continue;
        }
        /* PTE is already used to mapping before */
        if (!MmuPteIsInvalid(*pte)) {
            MMU_LOG(LOS_ERR_LEVEL, "Pte already mapped: level %u pte 0x%lx *pte 0x%016lx\n", level, (UINTPTR)pte, *pte);
            return LOS_ERRNO_MMU_PTE_BUSY;
        }
        levelSize = 1ULL << LN_TABLE_BITS_SHIFT(level);
        if (!IS_ALIGNED(va | pa, levelSize) || (sz < levelSize)) {
            table = PageTableAlloc();
            if (table == NULL) {
                return LOS_ERRNO_MMU_PAGETABLE_OOM;
            }
            MmuSetNextTableAddr(va, pte, (UINTPTR*)MMU_VA_TO_PA(table), level, 1);
            level++;
            continue;
        }

        /* Create block/page mapping */
        MmuSetBlkPageMapping(va, pte, pa, attrs, level);

        table = ptrTbl;
        level = FIRST_TABLE_LEVEL_INDEX;
        va += levelSize;
        pa += levelSize;
        sz -= levelSize;
    }
    return LOS_OK;
}

STATIC UINT32 MmuDestroyMapNoLock(UINTPTR *ptrTbl, UINTPTR vAddr, UINTPTR size)
{
    UINTPTR *pte = NULL;
    UINTPTR *table = ptrTbl;
    UINTPTR *pathPtes[MAX_PAGE_TABLE_LEVELS] = {NULL};
    UINTPTR levelSize, nextAddr;
    UINTPTR va = vAddr;
    UINTPTR sz = size;
    UINT32 level = FIRST_TABLE_LEVEL_INDEX;

    MMU_LOG(LOS_INFO_LEVEL, "Unmapping: vAddr 0x%lx size 0x%lx\n", va, sz);

    while (sz > 0) {
        pte = &table[LN_TABLE_ENTRY_INDEX(va, level)];
        pathPtes[level] = pte;

        /* Skip L0/1/2 table desc until block/page/invalid mapping */
        if (MmuPteIsNextTable(*pte, level)) {
            level++;
            table = MmuGetNextTableAddr(*pte);
            table = (UINTPTR*)MMU_PA_TO_VA(table);
            continue;
        }

        levelSize = 1ULL << LN_TABLE_BITS_SHIFT(level);
        MMU_LOG(LOS_DEBUG_LEVEL, "[Loop] vAddr 0x%lx size 0x%lx\n", va, sz);

        if (MmuPteIsInvalid(*pte)) {
            MMU_LOG(LOS_DEBUG_LEVEL, "IsPteInvalid [level%u] pte[0x%lx]:0x%lx\n", level, (UINTPTR)pte, *pte);
            nextAddr = ALIGN_DOWN(va, levelSize) + levelSize;
            if ((va + sz) < nextAddr) {
                /* No need to unmap repeatedly when the pte is already invalid */
                return LOS_OK;
            }
            /* Get gap between vAddr and nextAddr */
            levelSize = nextAddr - va;
            goto nextLoop;
        }

        if (!IS_ALIGNED(va, levelSize) || (sz < levelSize)) {
            /* Current mapping is superset, just unmapping part of it */
            table = MmuCopyToNextPte(va, pte, level);
            if (table == NULL) {
                return LOS_ERRNO_MMU_PAGETABLE_OOM;
            }
            level++;
            continue;
        }

        /* Set current pte invalid */
        MmuClearPteInvalid(va, pte, level);

        /* Free parent even grandparent table if unused */
        while ((level != FIRST_TABLE_LEVEL_INDEX) && PageTableIsReady(PageTableGetIdx(pte))) {
            PageTableFree(pte);
            pte = pathPtes[--level];
            MmuClearPteInvalid(va, pte, level);
        }

nextLoop:
        table = ptrTbl;
        level = FIRST_TABLE_LEVEL_INDEX;
        va += levelSize;
        sz -= levelSize;
    }

    return LOS_OK;
}

STATIC INLINE UINT32 MmuCreateMap(const CHAR *name, UINTPTR pAddr, UINTPTR vAddr, UINTPTR size, UINT32 attrs)
{
    return MmuCreateMapNoLock(g_firstPageTable, name, pAddr, vAddr, size, attrs);
}

STATIC INLINE UINT32 MmuDestroyMap(UINTPTR vAddr, UINTPTR size)
{
    return MmuDestroyMapNoLock(g_firstPageTable, vAddr, size);
}

UINT32 ArchMmuMap(UINTPTR vAddr, UINTPTR pAddr, UINTPTR size, UINT32 attrs)
{
    UINTPTR alignVAddr, alignPAddr, alignSize;
    UINT32 intSave;

    UINT32 ret = MmuCheckAddrParam(vAddr, pAddr, size, attrs);
    if (ret != LOS_OK) {
        goto ERR_OUT;
    }

    alignVAddr = ALIGN_DOWN(vAddr, LOSCFG_ARCH_ARM64_PAGE_SIZE);
    alignPAddr = ALIGN_DOWN(pAddr, LOSCFG_ARCH_ARM64_PAGE_SIZE);
    alignSize = ALIGN_UP(vAddr + size, LOSCFG_ARCH_ARM64_PAGE_SIZE) - alignVAddr;

    LOS_SpinLockSave(&g_mmuSpin, &intSave);
    ret = MmuCreateMap("usr name", alignPAddr, alignVAddr, alignSize, attrs);
    LOS_SpinUnlockRestore(&g_mmuSpin, intSave);
    if (ret != LOS_OK) {
        goto ERR_OUT;
    }

    MmuInvalidLocalTLB();

    return LOS_OK;

ERR_OUT:
    MMU_LOG(LOS_ERR_LEVEL, "ArchMmuMap() returned 0x%x.\n", ret);
    return ret;
}

UINT32 ArchMmuUnmap(UINTPTR vAddr, UINTPTR size)
{
    UINTPTR alignVAddr, alignSize;
    UINT32 intSave;

    /* Second vAddr and MMU_RW_NX_NORMAL_NC just make it OK, not checked yet */
    UINT32 ret = MmuCheckAddrParam(vAddr, vAddr, size, MMU_RW_NX_NORMAL_NC);
    if (ret != LOS_OK) {
        goto ERR_OUT;
    }

    alignVAddr = ALIGN_DOWN(vAddr, LOSCFG_ARCH_ARM64_PAGE_SIZE);
    alignSize = ALIGN_UP(vAddr + size, LOSCFG_ARCH_ARM64_PAGE_SIZE) - alignVAddr;

    LOS_SpinLockSave(&g_mmuSpin, &intSave);
    ret = MmuDestroyMap(alignVAddr, alignSize);
    LOS_SpinUnlockRestore(&g_mmuSpin, intSave);
    if (ret != LOS_OK) {
        goto ERR_OUT;
    }

    MmuInvalidLocalTLB();
    return LOS_OK;

ERR_OUT:
    MMU_LOG(LOS_ERR_LEVEL, "ArchMmuUnmap() returned 0x%x.\n", ret);
    return ret;
}

STATIC VOID MmuWalkTableNoLock(UINTPTR *ptrTbl, UINTPTR vAddr, UINTPTR size)
{
    UINTPTR *pte = NULL;
    UINTPTR *table = ptrTbl;
    UINTPTR levelSize;
    UINTPTR va = vAddr;
    UINTPTR sz = size;
    UINT32 level = FIRST_TABLE_LEVEL_INDEX;
    UINT8 visited[LOSCFG_ARCH_MMU_PAGE_TBL_NUM] = {0};
    UINT32 pageTblIdx;

    MMU_LOG(LOS_DEBUG_LEVEL, "[LiteOS] --- walk table begin ---\n");
    MMU_LOG(LOS_DEBUG_LEVEL, "Walk range: pagetable:0x%lx, vAddr:0x%lx, size:0x%lx\n", (UINTPTR)ptrTbl, va, sz);

    while (sz > 0) {
        pte = &table[LN_TABLE_ENTRY_INDEX(va, level)];
        /* Skip L0/1/2 table desc to block/page/invalid mapping */
        if (MmuPteIsNextTable(*pte, level)) {
            pageTblIdx = PageTableGetIdx(pte);
            if (visited[pageTblIdx] == 0) {
                MmuPrintPageItems(va, pte, level);
                visited[pageTblIdx] = 1;
            }
            level++;
            table = MmuGetNextTableAddr(*pte);
            table = (UINTPTR*)MMU_PA_TO_VA(table);
            continue;
        }

        levelSize = 1ULL << LN_TABLE_BITS_SHIFT(level);

        MmuPrintPageItems(va, pte, level);

        table = ptrTbl;
        level = FIRST_TABLE_LEVEL_INDEX;
        va += levelSize;
        sz -= levelSize;
    }

    MMU_LOG(LOS_DEBUG_LEVEL, "[LiteOS] --- walk table end ---\n");
}

UINT32 ArchMmuTableWalk(UINTPTR vAddr, UINTPTR size)
{
    UINT32 intSave;

    UINTPTR alignVAddr = ALIGN_DOWN(vAddr, LOSCFG_ARCH_ARM64_PAGE_SIZE);
    UINTPTR alignSize = ALIGN_UP(vAddr + size, LOSCFG_ARCH_ARM64_PAGE_SIZE) - alignVAddr;
    UINT32 ret = MmuCheckAddrParam(alignVAddr, 0, alignSize, MMU_RW_NX_NORMAL_NC);
    if (ret != LOS_OK) {
        MMU_LOG(LOS_ERR_LEVEL, "ArchMmuTableWalk() returned 0x%x.\n", ret);
        return ret;
    }
    LOS_SpinLockSave(&g_mmuSpin, &intSave);
    MmuWalkTableNoLock(g_firstPageTable, alignVAddr, alignSize);
    LOS_SpinUnlockRestore(&g_mmuSpin, intSave);
    return LOS_OK;
}

UINT32 ArchMmuTableStatistics(PageTablesStat *usage)
{
    UINT32 i, intSave;

    if (usage == NULL) {
        return LOS_ERRNO_MMU_INPUT_NULL;
    }

    LOS_SpinLockSave(&g_mmuSpin, &intSave);
    usage->pageTotal = LOSCFG_ARCH_MMU_PAGE_TBL_NUM;
    usage->pteTotal = LOSCFG_ARCH_MMU_PAGE_TBL_NUM * LN_TABLE_ENTRY_NUM;
    usage->pteUsed = 0;
    usage->pageUsed = 0;
    for (i = 0; i < LOSCFG_ARCH_MMU_PAGE_TBL_NUM; i++) {
        if (!PageTableIsFree(i)) {
            usage->pageUsed++;
            usage->pteUsed += (UINT32)PageTableModifyEntryUsedCnt(i, 0);
            MMU_LOG(LOS_DEBUG_LEVEL, "Page table index[%u], pte use counts:%u\n",
                    i, (UINT32)PageTableModifyEntryUsedCnt(i, 0));
        }
    }
    LOS_SpinUnlockRestore(&g_mmuSpin, intSave);

    MMU_LOG(LOS_INFO_LEVEL, "Total page num:%u and pte num:%u, used page num:%u and pte num:%u\n",
        usage->pageTotal, usage->pteTotal, usage->pageUsed, usage->pteUsed);

    return LOS_OK;
}

/*
 * The board provides every region through ArchMmuAppRegionReg()
 * (see targets/rk3568_m/liteos_m/arch/arm64/los_mmu.c).
 */
WEAK VOID ArchMmuDefaultRegionReg(VOID)
{
}

/* WEAK-empty default so the framework links standalone; the board glue
 * overrides this with its region table. */
WEAK VOID ArchMmuAppRegionReg(VOID)
{
}

STATIC VOID MmuRegionRegister(VOID)
{
    ArchMmuDefaultRegionReg();
    ArchMmuAppRegionReg();
}

VOID ArchMmuPageTablesCreate(VOID)
{
    const MmuMapRegion *region = NULL;
    UINT32 index;
    UINT32 ret;

    (VOID)memset(&g_regionInfo, 0, sizeof(g_regionInfo));

    PageTableInitManager();

    g_firstPageTable = PageTableAlloc();

    MmuRegionRegister();

    /* Setup mmu map regions, they cannot be overlapped with each other */
    for (index = 0U; index < g_regionInfo.usedCnt; index++) {
        region = &g_regionInfo.area[index];

        ret = MmuCreateMap(region->name, region->pAddr, region->vAddr, region->size, region->flags);
        if (ret != LOS_OK) {
            LOS_Panic("CreateMapNoLock returned:0x%x.\n", ret);
        }
    }
}
