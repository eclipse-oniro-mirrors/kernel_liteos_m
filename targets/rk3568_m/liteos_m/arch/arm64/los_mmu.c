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
 * RK3568 AMP slave MMU board glue (replaces the previous static identity
 * map). The generic dynamic framework lives in kernel/liteos_m/arch/arm64
 * (los_mmu.c / los_mmu_arm64.c / los_pagetable_mgr.c);
 * this file provides:
 *
 *   - ArchMmuAppRegionReg(): the board region table. The framework only
 *     accepts its predefined flag sets, so code pages are RO+X and data
 *     RW+NX - a hardening over the old static mapping (everything RW+X).
 *     Boundaries derive from board.ld linker symbols so the regions
 *     follow the image automatically.
 *
 *       entry  __text_entry_start .. ALIGN_UP(__text_entry_end, 4K)  RO+X WB
 *              (.text.entry + empty .vectors pad)
 *       initdata ALIGN_UP(__text_entry_end,4K) .. __text_start     RW+NX WB
 *              (sysinitcall + g_hwiForm + init_array: WRITTEN at run
 *               time - ArchInit fills the hwi form; board.ld keeps this
 *               block page-aligned between the entry page and .text)
 *       text   __text_start .. ALIGN_UP(__text_end, 4K)             RO+X WB
 *       data   ALIGN_UP(__text_end, 4K) .. ALIGN_UP(__heap_end, 2M) RW+NX WB
 *              (rodata/data/resource_table/bss/stacks/heap)
 *       shared window 0x74000000 + 1M                     RW+NX  Normal NC
 *              (openamp vrings + rpmsg buffers + net ctrl; master-
 *              allocated, NO side does cache maintenance on it, so it
 *              must not be Normal-WB. Device was wrong too: rpmsg
 *              payloads sit at 4-byte-aligned offsets inside the 512B
 *              buffers and 8-byte memcpy block copies (len >= 8) fault
 *              on Device mappings)
 *       net DMA pool 0x77A00000 + 2M                      RW+NX  Normal NC
 *              (GMAC descriptors + frames, not IO-coherent)
 *       boot-probe scratch 0x77C00000 + 2M                RW+NX  Device nGnRE
 *       peripherals 0xFD000000 + 32M                      RW+NX  Device nGnRE
 *              (GIC-600 GICD/GICR + UART2 0xFE660000)
 *
 *     The 1MB shared/image boundary is not 2MB aligned: the framework
 *     handles it by splitting that L2 entry into an L3 table automatically
 *     (what the static version did by hand through g_l3_shared).
 *
 *   - LosMmuInit(): called from reset_vector.S (unchanged call site) -
 *     builds the tables, drops any stale D-cache lines left by the previous
 *     world (AMP hand-over), turns the MMU on (ArchMmuConfig) and then
 *     enables the I/D caches.
 */

#include "los_typedef.h"
#include "los_toolchain.h"
#include "arch/regs.h"
#include "arch/cache.h"
#include "arch/mmu.h"
#include "arch/mmu_pri.h"

/* board.ld symbols */
extern UINTPTR __text_entry_start;
extern UINTPTR __text_entry_end;
extern UINTPTR __text_start;
extern UINTPTR __text_end;
extern UINTPTR __heap_end;

/* net DMA pool window (descriptors + frame buffers), placed at the
 * tail of the slave-private region (see board.ld); must match
 * NET_DMA_BASE in gmac1_profile.h. */
#define NET_DMA_BASE      0x77A00000UL
#define NET_DMA_SIZE      0x200000UL

/* openamp shared window (vrings + net ctrl @ 0x740ff000), see mica_cfg.h */
#define SHMEM_BASE        0x74000000UL
#define SHMEM_SIZE        0x100000UL

/* UMT shared buffer window (master mcs_km pins phy_addr from the dts
 * oh_rt,copy-mem-region node, slave send_buffer = phy + 1MB, both
 * inside this 2MB window at the tail of the 64MB mcs window).
 * Normal-NC like the other shared windows. */
#define UMT_BASE           0x77E00000UL
#define UMT_SIZE           0x200000UL

/* boot-probe scratch area */
#define SCRATCH_BASE      0x77C00000UL
#define SCRATCH_SIZE      0x200000UL

/* peripherals: GIC-600 (GICD 0xFD400000 / GICR 0xFD580000) and UART2
 * 0xFE660000, covered as Device blocks */
#define PERIPH_BASE       0xFD000000UL
#define PERIPH_SIZE       0x2000000UL

#define PAGE_SIZE_4K      LOSCFG_ARCH_ARM64_PAGE_SIZE
#define BLOCK_SIZE_2M     0x200000UL

/* Board region table for the dynamic MMU framework. The image is split
 * around the init-table block (board.ld keeps sysinitcall + g_hwiForm +
 * init_array page-aligned between the entry page and .text): that block
 * is written at run time (ArchInit fills the hwi form) and must be RW,
 * while the code pages stay RO+X. */
VOID ArchMmuAppRegionReg(VOID)
{
    UINTPTR entryStart = (UINTPTR)&__text_entry_start;
    UINTPTR entryEnd = ALIGN_UP((UINTPTR)&__text_entry_end, PAGE_SIZE_4K);
    UINTPTR textStart = (UINTPTR)&__text_start;
    UINTPTR textEnd = ALIGN_UP((UINTPTR)&__text_end, PAGE_SIZE_4K);
    UINTPTR dataEnd = ALIGN_UP((UINTPTR)&__heap_end, BLOCK_SIZE_2M);
    MmuMapRegion regions[] = {
        {"entry", entryStart, entryStart, entryEnd - entryStart, MMU_RO_X_NORMAL_WB},
        {"initdata", entryEnd, entryEnd, textStart - entryEnd, MMU_RW_NX_NORMAL_WB},
        {"text", textStart, textStart, textEnd - textStart, MMU_RO_X_NORMAL_WB},
        {"data", textEnd, textEnd, dataEnd - textEnd, MMU_RW_NX_NORMAL_WB},
        {"shmem", SHMEM_BASE, SHMEM_BASE, SHMEM_SIZE, MMU_RW_NX_NORMAL_NC},
        {"netdma", NET_DMA_BASE, NET_DMA_BASE, NET_DMA_SIZE, MMU_RW_NX_NORMAL_NC},
        {"umt", UMT_BASE, UMT_BASE, UMT_SIZE, MMU_RW_NX_NORMAL_NC},
        {"scratch", SCRATCH_BASE, SCRATCH_BASE, SCRATCH_SIZE, MMU_RW_NX_DEVICE_MAP},
        {"periph", PERIPH_BASE, PERIPH_BASE, PERIPH_SIZE, MMU_RW_NX_DEVICE_MAP},
    };
    UINT32 ret;
    UINT32 i;

    for (i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
        ret = ArchMmuRegionInsert(&regions[i]);
        if (ret != LOS_OK) {
            PRINT_ERR("Region[%s] register failed, ret:0x%x.\n", regions[i].name, ret);
        }
    }
}

/* MMU bring-up entry, called from reset_vector.S after .bss clearing
 * (replaces the static-table LosMmuInit of the previous implementation). */
VOID LosMmuInit(VOID)
{
    UINT64 sctlr;

    /* Build the page tables from the board region table (MMU still off,
     * tables land in the .bss pool). */
    ArchMmuPageTablesCreate();

    /* Drop any dirty line the previous world (Linux/TF-A/OP-TEE) may have
     * left behind before the D-cache starts caching. The slave is entered
     * via PSCI CPU_ON (architecturally invalidated L1), so this is defence
     * in depth for warm paths (QEMU -kernel, direct load). */
    ArchDCacheInvalidate();

    /* MAIR/TCR/TTBR0_EL1 + MMU on (framework path). */
    ArchMmuConfig();

    /* Enable the I/D caches: drop stale I-cache state from the previous
     * world first. */
    __asm__ volatile("ic iallu" : : : "memory");
    __asm__ volatile("dsb sy; isb" : : : "memory");
    sctlr = AARCH64_SYSREG_READ(sctlr_el1);
    sctlr |= (1ULL << 12) | (1ULL << 2);   /* I | C */
    AARCH64_SYSREG_WRITE(sctlr_el1, sctlr);
}
