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
 * RK3588 AMP slave MMU board glue (replaces the previous static identity
 * map). The generic dynamic framework lives in kernel/liteos_m/arch/arm64
 * (los_mmu.c / los_mmu_arm64.c / los_pagetable_mgr.c); this file provides:
 *
 *   - ArchMmuAppRegionReg(): the board region table. Boundaries derive
 *     from board.ld linker symbols so the regions follow the image
 *     automatically; window addresses must stay in sync with
 *     board/include/soc.h and the master-side mcs reserved window.
 *
 *       entry  __text_entry_start .. ALIGN_UP(__text_entry_end, 4K)  RO+X WB
 *       initdata ALIGN_UP(__text_entry_end,4K) .. __text_start     RW+NX WB
 *       text   __text_start .. ALIGN_UP(__text_end, 4K)             RO+X WB
 *       data   ALIGN_UP(__text_end, 4K) .. ALIGN_UP(__heap_end, 2M) RW+NX WB
 *              (rodata/data/resource_table/bss/stacks/heap)
 *       shared window 0x74000000 + 1M                     RW+NX  Normal NC
 *              (openamp vrings + rpmsg buffers; master-allocated, neither
 *              side does cache maintenance on it, so it must not be
 *              Normal-WB. Device was wrong too: rpmsg payloads sit at
 *              4-byte-aligned offsets inside the 512B buffers and
 *              8-byte memcpy block copies (len >= 8) fault on Device
 *              mappings)
 *       net DMA pool 0x76000000 + 2M                      RW+NX  Normal NC
 *              (GMAC1 descriptors + frames, not IO-coherent)
 *       boot-probe scratch 0x77E00000 + 2M                RW+NX  Device nGnRE
 *       peripherals 0xF0000000 + 240M                     RW+NX  Device nGnRE
 *              (GIC-600 GICD 0xFE600000 / GICR 0xFE680000, UART2
 *              0xFEB50000, GMAC1 0xFE1C0000, CAN1 0xFEA60000 /
 *              CAN2 0xFEA70000, CRU & friends - see soc.h)
 *
 *   - LosMmuInit(): called from reset_vector.S (unchanged call site) -
 *     builds the tables, drops any stale D-cache lines left by the previous
 *     world (AMP hand-over), turns the MMU on (ArchMmuConfig) and then
 *     enables the I/D caches.
 */

#include <string.h>
#include "los_typedef.h"
#include "los_debug.h"

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

/* net DMA pool window (descriptors + frame buffers); must match
 * NET_DMA_BASE in board/net/gmac1_profile.h. */
#define NET_DMA_BASE      0x76000000UL
#define NET_DMA_SIZE      0x200000UL

/* openamp shared window (vrings, master-allocated); see soc.h. */
#define SHMEM_BASE        0x74000000UL
#define SHMEM_SIZE        0x100000UL

/* boot-probe scratch area */
#define SCRATCH_BASE      0x77E00000UL
#define SCRATCH_SIZE      0x200000UL

/* peripherals: keep in sync with PERIPH_BASE_MMU / PERIPH_SIZE_MMIO in
 * board/include/soc.h (256M window covering GIC/UART/GMAC/CAN/CRU). */
#define PERIPH_BASE       0xF0000000UL
#define PERIPH_SIZE       0x0F000000UL /* ends at 0xFF000000: end == 4G is rejected by the framework (VA_BITS=32) */

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

    /* NOTE: no ArchDCacheInvalidate() here, unlike the rk3568_m glue.
     * On RK3588 the DSU L3 is the shared point of coherency and
     * CLIDR_EL1.LoC includes it, so a set/way invalidate would DISCARD
     * the still-dirty L3 lines holding the master's ELF-segment copy
     * (the master writes the segments cacheable and never flushes; the
     * slave is expected to snoop them through the shared L3 - exactly
     * how the previous static-map build worked). The slave's private L1
     * is architecturally invalidated by PSCI CPU_ON anyway. */

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
