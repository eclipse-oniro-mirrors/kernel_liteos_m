/*
 * Copyright (c) 2024 HiHope Open Source Organization.
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
 *
 * Description: PMP initialization for OHOS SRAM.
 *
 * Configures PMP entry 5 to cover the OHOS-used SRAM range with
 * NORMAL (WB+RALLOC) attribute, RWX permission, locked.
 *
 * Background:
 *   - bootloader (flashboot) locks entries 0-4 to cover ROM/DTCM/Flash
 *   - entries 5-15 are left OFF by bootloader
 *   - Without an active PMP entry, SRAM defaults to DEVICE attribute,
 *     which traps on misaligned access (root cause of crash)
 *   - This module activates entry 5 to cover OHOS SRAM as NORMAL
 *
 * Design doc: 初始化pmp解析.md §11
 */

#include <stdint.h>
#include "los_debug.h"            /* PRINTK */
#include "pmp_init.h"

/* Symbols defined in board.ld — boundaries of the OHOS-used SRAM range */
extern char __sram_start__[];
extern char __sram_end__[];

/* WS63 PMP attribute values (mirror enum pmp_attr_t in drv_pmp.h) */
#define PMP_ATTR_WRITEBACK_RALLOCATE  0x7    /* NORMAL, WB + read-allocate */

void pmp_init(void)
{
    uint32_t sram_start   = (uint32_t)(uintptr_t)__sram_start__;
    uint32_t sram_end     = (uint32_t)(uintptr_t)__sram_end__;
    uint32_t pmpaddr5_val = sram_end >> 2;   /* G=0: pmpaddr = phys_addr >> 2 */

    /*
     * pmpcfg byte for entry 5:
     *   bit 7 (L)   = 1   locked
     *   bit 6       = 0   reserved
     *   bit 5       = 0   reserved
     *   bit 4-3 (A) = 01  TOR
     *   bit 2 (X)   = 1   execute
     *   bit 1 (W)   = 1   write
     *   bit 0 (R)   = 1   read
     *   → 0b1000_1111 = 0x8F
     */
    const uint32_t pmpcfg_byte = 0x8F;

    /* MEMATTRL nibble 5 sits at bits 23:20 (entry_index * 4) */
    const uint32_t attr_nibble = PMP_ATTR_WRITEBACK_RALLOCATE;
    const uint32_t attr_shift  = 5 * 4;

    uint32_t v;

    PRINTK("\n[PMP_INIT] configuring entry 5: TOR [0x%x, 0x%x), attr=WB+RALLOC, RWX, lock\n",
           sram_start, sram_end);
    PRINTK("[PMP_INIT] pmpaddr5 = 0x%x (top addr 0x%x)\n", pmpaddr5_val, sram_end);

    /*
     * Write order matters: pmpaddr + MEMATTR first, then pmpcfg.A.
     * The moment A=TOR is set, the entry becomes active and uses the
     * already-written address/attr; writing A first would briefly
     * activate with stale/garbage pmpaddr5.
     */

    /* 1) Write pmpaddr5 = SRAM upper bound (TOR exclusive) */
    __asm__ volatile ("csrw pmpaddr5, %0" :: "r"(pmpaddr5_val) : "memory");

    /* 2) Update MEMATTRL (CSR 0x7d8) nibble 5 = 0x7 (WB+RALLOC) */
    __asm__ volatile ("csrr %0, 0x7d8" : "=r"(v));
    v = (v & ~(0xFUL << attr_shift)) | (attr_nibble << attr_shift);
    __asm__ volatile ("csrw 0x7d8, %0" :: "r"(v) : "memory");
    PRINTK("[PMP_INIT] MEMATTRL = 0x%x (entry 5 nibble=0x%x)\n", v, attr_nibble);

    /* 3) Write pmpcfg1 byte 1 (entry 5) = 0x8F: A=TOR + L=1, activates + locks */
    __asm__ volatile ("csrr %0, pmpcfg1" : "=r"(v));
    v = (v & ~(0xFFUL << 8)) | (pmpcfg_byte << 8);
    __asm__ volatile ("csrw pmpcfg1, %0" :: "r"(v) : "memory");
    PRINTK("[PMP_INIT] pmpcfg1 = 0x%x (entry 5 cfg=0x%x, lock=true)\n", v, pmpcfg_byte);

    /* 4) Memory barrier ensures config is committed before any SRAM access */
    __asm__ volatile ("fence" ::: "memory");
    PRINTK("[PMP_INIT] PMP entry 5 active & locked. SRAM now NORMAL.\n");
}
