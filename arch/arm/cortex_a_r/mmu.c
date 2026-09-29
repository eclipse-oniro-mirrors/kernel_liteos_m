/*
 * Copyright (c) 2024 morpheus
 *
 *
 *   DDR:           0x80000000, 512MB (0x20000000)
 *                    GIC  0x10300000 (GICD +0x1000, GICC +0x2000)
 *                    UART 0x120a0000
 *
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

/* [16] S(Shareable) */
#define MMU_L1_SECTION_SHAREABLE (0x1 << 16)

#define MMU_DESCRIPTOR_L1_SMALL_ENTRY_NUMBERS 0x4000U
#define MMU_L1_SECTION_SIZE 0x100000U /* 1MB */
#define MMU_L1_SECTION_SHIFT 20

__attribute__((aligned(0x4000))) static UINT32 g_firstPageTable[MMU_DESCRIPTOR_L1_SMALL_ENTRY_NUMBERS];

#define SYS_MEM_BASE 0x80000000U
#define SYS_MEM_SIZE 0x20000000U /* 512MB */

#define PERIPH_MEM_BASE 0x10000000U
#define PERIPH_MEM_SIZE 0x10000000U /* 256MB → 0x10000000-0x1FFFFFFF */

#define PERIPH_HI_BASE 0xF0000000U
#define PERIPH_HI_SIZE 0x10000000U /* 256MB */

#define MMU_FLAGS_DDR                                  \
    (MMU_L1_TYPE_SECTION | MMU_L1_SECTION_DOMAIN(0) |  \
     MMU_L1_SECTION_AP_RW | MMU_L1_SECTION_MEM_CACHE | \
     MMU_L1_SECTION_SHAREABLE)

#define MMU_FLAGS_PERIPH                                \
    (MMU_L1_TYPE_SECTION | MMU_L1_SECTION_DOMAIN(0) |   \
     MMU_L1_SECTION_AP_RW | MMU_L1_SECTION_MEM_DEVICE | \
     MMU_L1_SECTION_XN | MMU_L1_SECTION_SHAREABLE)

static VOID OsMmuMapRange(UINT32 physBase, UINT32 size, UINT32 flags)
{
    UINT32 num = size >> MMU_L1_SECTION_SHIFT;
    UINT32 i;

    for (i = 0; i < num; i++)
    {
        UINT32 pa = physBase + (i << MMU_L1_SECTION_SHIFT);
        UINT32 vidx = pa >> MMU_L1_SECTION_SHIFT;
        if (vidx >= MMU_DESCRIPTOR_L1_SMALL_ENTRY_NUMBERS)
        {
            break;
        }

        g_firstPageTable[vidx] = (pa & 0xFFF00000U) | flags;
    }
}

VOID OsMmuInit(VOID)
{
    UINT32 i;

    for (i = 0; i < MMU_DESCRIPTOR_L1_SMALL_ENTRY_NUMBERS; i++)
    {
        g_firstPageTable[i] = 0;
    }

    OsMmuMapRange(SYS_MEM_BASE, SYS_MEM_SIZE, MMU_FLAGS_DDR);
    OsMmuMapRange(PERIPH_MEM_BASE, PERIPH_MEM_SIZE, MMU_FLAGS_PERIPH);
    OsMmuMapRange(PERIPH_HI_BASE, PERIPH_HI_SIZE, MMU_FLAGS_PERIPH);

    __asm__ volatile(
        "mov r0, #0\n"
        "mcr p15, 0, r0, c8, c7, 0\n" /* Invalidate entire TLB */
        "mcr p15, 0, r0, c7, c5, 0\n" /* Invalidate I-cache (IALL) */
        "mcr p15, 0, r0, c7, c5, 6\n" /* Invalidate BTB */
        "dsb\n"
        "isb" ::: "r0");

    __asm__ volatile(
        "mov r0, #0\n"
        "mcr p15, 0, r0, c2, c0, 1\n" /* TTBCR = 0 */
        "isb" ::: "r0");

    __asm__ volatile(
        "orr r0, %0, #0x4a\n"         /* Inner cacheable + Outer shareable(S=1) */
        "mcr p15, 0, r0, c2, c0, 0\n" /* TTBR0 */
        "isb" :: "r"(&g_firstPageTable) : "r0");

    __asm__ volatile(
        "mov r0, #1\n"                /* domain0=01(client) */
        "mcr p15, 0, r0, c3, c0, 0\n" /* DACR */
        "isb" ::: "r0");

    __asm__ volatile(
        "mrc p15, 0, r0, c1, c0, 0\n"
        "bic r0, r0, #0x2000\n"
        "bic r0, r0, #0x10000000\n"
        "orr r0, r0, #0x1\n"
        "bic r0, r0, #0x2\n"
        "orr r0, r0, #0x1000\n"       /* I:I-cache */
        "orr r0, r0, #0x4\n"          /* C:D-cache */
        "orr r0, r0, #0x800000\n"     /* Z:branch prediction(bit23) */
        "orr r0, r0, #0x80000000\n"   /* BR:bit31 */
        "mcr p15, 0, r0, c1, c0, 0\n"
        "isb" ::: "r0");

    __asm__ volatile(
        "mov r0, #0\n"
        "mcr p15, 0, r0, c8, c7, 0\n" /* Invalidate TLB */
        "dsb\n"
        "isb" ::: "r0");
}
