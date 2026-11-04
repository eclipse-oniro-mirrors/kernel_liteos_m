/*
 * Copyright (c) 2020-2022 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "los_typedef.h"

#define MMU_L1_TYPE_SECTION (0x2)
#define MMU_L1_SECTION_XN (0x1 << 4)
#define MMU_L1_SECTION_DOMAIN_SHIFT 5
#define MMU_L1_SECTION_DOMAIN(n) ((n) << MMU_L1_SECTION_DOMAIN_SHIFT)
#define MMU_L1_SECTION_AP_RW (0x3 << 10)
#define MMU_L1_SECTION_TEX_SHIFT 12
#define MMU_L1_SECTION_C (0x1 << 3)
#define MMU_L1_SECTION_B (0x1 << 2)
#define MMU_L1_SECTION_MEM_CACHE ((0x1 << MMU_L1_SECTION_TEX_SHIFT) | MMU_L1_SECTION_C | MMU_L1_SECTION_B)
#define MMU_L1_SECTION_MEM_DEVICE (0)
#define MMU_L1_SECTION_SHAREABLE (0x1 << 16)
#define MMU_DESCRIPTOR_L1_SMALL_ENTRY_NUMBERS 0x4000U
#define MMU_L1_SECTION_SIZE 0x100000U
#define MMU_L1_SECTION_SHIFT 20

__attribute__((aligned(0x4000))) static UINT32 g_firstPageTable[MMU_DESCRIPTOR_L1_SMALL_ENTRY_NUMBERS];

#define SYS_MEM_BASE 0x80000000U
#define SYS_MEM_SIZE 0x20000000U
#define PERIPH_MEM_BASE 0x10000000U
#define PERIPH_MEM_SIZE 0x10000000U
#define PERIPH_HI_BASE 0xF0000000U
#define PERIPH_HI_SIZE 0x10000000U

#define MMU_FLAGS_DDR                                  \
    (MMU_L1_TYPE_SECTION | MMU_L1_SECTION_DOMAIN(0) |  \
     MMU_L1_SECTION_AP_RW | MMU_L1_SECTION_MEM_CACHE | \
     MMU_L1_SECTION_SHAREABLE)

#define MMU_FLAGS_PERIPH                                \
    (MMU_L1_TYPE_SECTION | MMU_L1_SECTION_DOMAIN(0) |   \
     MMU_L1_SECTION_AP_RW | MMU_L1_SECTION_MEM_DEVICE | \
     MMU_L1_SECTION_XN | MMU_L1_SECTION_SHAREABLE)

#define BOOTROM_VIRT_BASE 0xE0000000U

static VOID OsMmuMapRangeVirt(UINT32 vaddr, UINT32 physBase, UINT32 size, UINT32 flags)
{
    UINT32 num = size >> MMU_L1_SECTION_SHIFT;
    UINT32 i;

    for (i = 0; i < num; i++)
    {
        UINT32 pa = physBase + (i << MMU_L1_SECTION_SHIFT);
        UINT32 vidx = (vaddr + (i << MMU_L1_SECTION_SHIFT)) >> MMU_L1_SECTION_SHIFT;
        if (vidx >= MMU_DESCRIPTOR_L1_SMALL_ENTRY_NUMBERS)
        {
            break;
        }

        g_firstPageTable[vidx] = (pa & 0xFFF00000U) | flags;
    }
}

static VOID OsMmuMapRange(UINT32 physBase, UINT32 size, UINT32 flags)
{
    OsMmuMapRangeVirt(physBase, physBase, size, flags);
}

VOID OsMmuEnable(VOID)
{
    __asm__ volatile(
        "mov r0, #0\n"
        "mcr p15, 0, r0, c8, c7, 0\n"
        "mcr p15, 0, r0, c7, c5, 0\n"
        "mcr p15, 0, r0, c7, c5, 6\n"
        "dsb\n"
        "isb" ::: "r0");

    __asm__ volatile(
        "mov r0, #0\n"
        "mcr p15, 0, r0, c2, c0, 1\n"
        "isb" ::: "r0");

    __asm__ volatile(
        "orr r0, %0, #0x4a\n"
        "mcr p15, 0, r0, c2, c0, 0\n"
        "isb" :: "r"(&g_firstPageTable) : "r0");

    __asm__ volatile(
        "mov r0, #1\n"
        "mcr p15, 0, r0, c3, c0, 0\n"
        "isb" ::: "r0");

    __asm__ volatile(
        "mrc p15, 0, r0, c1, c0, 1\n"
        "orr r0, r0, #(1 << 6)\n"
        "orr r0, r0, #(1 << 2)\n"
        "orr r0, r0, #(1 << 11)\n"
        "mcr p15, 0, r0, c1, c0, 1\n"
        "dsb" ::: "r0");

    __asm__ volatile(
        "mrc p15, 0, r0, c1, c0, 0\n"
        "bic r0, r0, #0x2000\n"
        "bic r0, r0, #0x10000000\n"
        "orr r0, r0, #0x1\n"
        "bic r0, r0, #0x2\n"
        "orr r0, r0, #0x1000\n"
        "orr r0, r0, #0x4\n"
        "orr r0, r0, #0x800000\n"
        "orr r0, r0, #0x80000000\n"
        "mcr p15, 0, r0, c1, c0, 0\n"
        "isb" ::: "r0");

    __asm__ volatile(
        "mov r0, #0\n"
        "mcr p15, 0, r0, c8, c7, 0\n"
        "dsb\n"
        "isb" ::: "r0");
}

VOID OsMmuInit(VOID)
{
    UINT32 i;

    for (i = 0; i < MMU_DESCRIPTOR_L1_SMALL_ENTRY_NUMBERS; i++)
    {
        g_firstPageTable[i] = 0;
    }

    OsMmuMapRangeVirt(BOOTROM_VIRT_BASE, 0x0, 0x100000U, MMU_FLAGS_PERIPH);
    OsMmuMapRange(SYS_MEM_BASE, SYS_MEM_SIZE, MMU_FLAGS_DDR);
    OsMmuMapRange(PERIPH_MEM_BASE, PERIPH_MEM_SIZE, MMU_FLAGS_PERIPH);
    OsMmuMapRange(PERIPH_HI_BASE, PERIPH_HI_SIZE, MMU_FLAGS_PERIPH);

    OsMmuEnable();
}
