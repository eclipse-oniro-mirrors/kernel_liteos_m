/*
 * Minimal identity-mapped MMU for the RK3568 AMP slave core (LiteOS-M).
 *
 * PSCI hands this core over with the MMU off, which makes ALL memory
 * Device-nGnRnE. On Device memory every unaligned wide access raises an
 * alignment fault (ESR DFSC=0x21): compiler-merged stores in los_task.c,
 * 4-byte-aligned heap nodes from the memalign gap-node path in
 * los_memory.c, ... all kill the kernel mid-init. Mapping the firmware
 * image as Normal memory makes unaligned accesses architecturally legal
 * again - this is what the UniProton port on this board has always done.
 *
 * Policy: D-cache ON (Normal WB for the whole RTOS runtime - image, heap,
 * stacks; the runtime space is "normal cache" as required for real-time
 * performance). Regions that must bypass the D-cache get their own
 * attributes: the openamp shared window stays Device (the master
 * polls/DMAs it without going through our caches) and the EtherCAT DMA
 * pools are Normal Non-Cacheable (the GMAC is not IO-coherent).
 *
 * Layout (PA == VA identity, 4K granule):
 *   0x74000000-0x74100000  Device nGnRE  shared window (openamp vrings
 *                                           + net ctrl @ 0x740ff000)
 *   0x74100000-0x74A00000  Normal WB    image: text/data/bss/heap/stack
 *   0x76000000-0x76200000  Normal NC    net DMA pools (descriptors
 *                                           + frame buffers; see
 *                                           gmac1_profile.h NET_DMA_*)
 *   0x77E00000-0x78000000  Device nGnRE  boot-probe scratch area
 *   0xFD000000-0xFF000000  Device nGnRE  GIC-600 + UART2
 *
 * The page tables live in .bss as 4K-aligned static arrays: startup.S
 * zeroes .bss before calling LosMmuInit(), so the tables start clean and
 * this function only has to fill them in (runs with the MMU still off -
 * keep this file strict-align clean, no wide unaligned accesses).
 */

typedef unsigned long u64;

/* MAIR_EL1 attribute indices */
#define ATTR_DEVICE    0UL
#define ATTR_NORMAL    1UL   /* WB RA/WA: RTOS runtime (image/heap/stacks) */
#define ATTR_NORMAL_NC 2UL   /* non-cacheable: net DMA pools */

/* net DMA pool window (descriptors + frame buffers). Must match
 * NET_DMA_BASE in device/board/rockchip/rk3568/liteos_m/board/
 * driver/net/gmac1_profile.h. One 2MB L2 block, outside the image
 * region, far from the 4MB heap so it cannot collide with heap growth. */
#define NET_DMA_BASE  0x76000000UL

/* ARMv8 AArch64 stage-1 descriptor bits */
#define PTE_VALID      (1UL << 0)
#define PTE_TABLE      (3UL << 0)  /* valid + table descriptor */
#define PTE_BLOCK      (1UL << 0)  /* valid + block descriptor */
#define PTE_PAGE       (3UL << 0)  /* valid + page descriptor (level 3) */
#define PTE_ATTRIDX(x) ((u64)(x) << 2)
#define PTE_SH_ISH     (3UL << 8)  /* inner shareable */
#define PTE_AF         (1UL << 10)

#define L1_INDEX(va)  (((va) >> 30) & 0x1FF)
#define L2_INDEX(va)  (((va) >> 21) & 0x1FF)

#define DEVICE_PTE(pa) \
    ((pa) | PTE_BLOCK | PTE_ATTRIDX(ATTR_DEVICE) | PTE_SH_ISH | PTE_AF)
#define NORMAL_PTE(pa) \
    ((pa) | PTE_BLOCK | PTE_ATTRIDX(ATTR_NORMAL) | PTE_SH_ISH | PTE_AF)
#define NC_PTE(pa) \
    ((pa) | PTE_BLOCK | PTE_ATTRIDX(ATTR_NORMAL_NC) | PTE_SH_ISH | PTE_AF)
#define DEVICE_PAGE(pa) \
    ((pa) | PTE_PAGE | PTE_ATTRIDX(ATTR_DEVICE) | PTE_SH_ISH | PTE_AF)
#define NORMAL_PAGE(pa) \
    ((pa) | PTE_PAGE | PTE_ATTRIDX(ATTR_NORMAL) | PTE_SH_ISH | PTE_AF)

/* 4K granule: 512 entries of 8 bytes per table level */
static volatile u64 g_l1[512]        __attribute__((aligned(4096)));
static volatile u64 g_l2_low[512]    __attribute__((aligned(4096)));  /* 0x40000000-0x7FFFFFFF */
static volatile u64 g_l2_high[512]   __attribute__((aligned(4096)));  /* 0xC0000000-0xFFFFFFFF */
static volatile u64 g_l3_shared[512] __attribute__((aligned(4096)));  /* 0x74000000-0x74200000 */

/* Invalidate the whole D-cache by set/way before enabling C. The slave
 * is entered via PSCI CPU_ON (architecturally invalidated L1), so this
 * is defence in depth for warm paths (QEMU -kernel, direct load) where
 * the previous world may have left dirty lines that would otherwise be
 * evicted on top of our freshly written .bss/page tables.
 * Operand format (same as Linux/TF-A cache maintenance):
 *   level << 1 | set << log2(line) | way << clz32(assoc-1) */
static void dcache_invalidate_all(void)
{
    u64 clidr;
    int level, loc;

    __asm__ volatile("mrs %0, clidr_el1" : "=r"(clidr));
    loc = (int)((clidr >> 24) & 7UL);   /* CLIDR_EL1.LoC */
    for (level = 0; (level < loc) && (level < 7); level++) {
        u64 ctype = (clidr >> (3 * level)) & 7UL;
        u64 ccsidr;
        int lshift, sets, ways, wshift, set, way;

        if (ctype < 2) {                /* 2 = D-only, 3/4 = have D cache */
            continue;
        }
        __asm__ volatile("msr csselr_el1, %0" :: "r"((u64)(level << 1)));
        __asm__ volatile("isb");
        __asm__ volatile("mrs %0, ccsidr_el1" : "=r"(ccsidr));
        lshift = (int)((ccsidr & 7UL) + 4UL);              /* log2(line) */
        sets   = (int)(((ccsidr >> 13) & 0x7FFFUL) + 1UL);
        ways   = (int)(((ccsidr >> 3) & 0x3FFUL) + 1UL);
        wshift = (ways > 1) ? __builtin_clz((unsigned int)(ways - 1)) : 32;
        for (way = ways - 1; way >= 0; way--) {
            for (set = sets - 1; set >= 0; set--) {
                u64 sw = ((u64)way << wshift) |
                         ((u64)set << lshift) |
                         ((u64)level << 1);
                __asm__ volatile("dc isw, %0" :: "r"(sw) : "memory");
            }
        }
    }
    __asm__ volatile("msr csselr_el1, %0" :: "r"(0UL));
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

static void MmuEnable(u64 ttbr0)
{
    const u64 mair = (0x04UL << (ATTR_DEVICE * 8)) |
                     (0xFFUL << (ATTR_NORMAL * 8)) |
                     (0x44UL << (ATTR_NORMAL_NC * 8));
    /* TCR_EL1: IPS=32bit, TG0=4K, SH0=inner, ORGN0/IRGN0=WBWA (page-walk
     * cacheable), EPD1=1 (TTBR1 walks disabled), T0SZ=32 */
    const u64 tcr = (1UL << 23) | (3UL << 12) | (1UL << 10) | (1UL << 8) | 32UL;
    u64 sctlr;

    __asm__ volatile("dsb    sy" : : : "memory");
    __asm__ volatile("tlbi   vmalle1" : : : "memory");
    __asm__ volatile("dsb    sy" : : : "memory");
    __asm__ volatile("msr    mair_el1, %0" : : "r"(mair) : "memory");
    __asm__ volatile("msr    tcr_el1,  %0" : : "r"(tcr) : "memory");
    __asm__ volatile("msr    ttbr0_el1, %0" : : "r"(ttbr0) : "memory");
    __asm__ volatile("isb" : : : "memory");
    __asm__ volatile("ic     iallu" : : : "memory");
    __asm__ volatile("dsb    sy; isb" : : : "memory");
    __asm__ volatile("mrs    %0, sctlr_el1" : "=r"(sctlr) : : "memory");
    /* D-cache ON (C, bit 2) with the runtime space Normal-WB: invalidate
     * first so no stale line from the previous world can hit DDR later.
     * Keep A (bit 1) off as before. */
    dcache_invalidate_all();
    sctlr = (sctlr & ~2UL) | 1UL | 4UL | 4096UL;
    __asm__ volatile("msr    sctlr_el1, %0" : : "r"(sctlr) : "memory");
    __asm__ volatile("isb" : : : "memory");
}

void LosMmuInit(void)
{
    u64 pa;
    int i;


    /* level 1: point the two 1GB slots we use at their level-2 tables */
    g_l1[L1_INDEX(0x74100000UL)] = PTE_TABLE | (u64)(unsigned long)g_l2_low;
    g_l1[L1_INDEX(0xFD400000UL)] = PTE_TABLE | (u64)(unsigned long)g_l2_high;

    /* image region 0x74200000-0x74A00000 as 2MB Normal blocks (the first
     * 2MB of the image comes from g_l3_shared below) */
    for (pa = 0x74200000UL; pa < 0x74A00000UL; pa += 0x200000UL) {
        g_l2_low[L2_INDEX(pa)] = NORMAL_PTE(pa);
    }


    /* boot-probe scratch area 0x77E00000-0x78000000 */
    g_l2_low[L2_INDEX(0x77E00000UL)] = DEVICE_PTE(0x77E00000UL);

    /* net DMA pools: one 2MB Normal Non-Cacheable block at 0x76000000
     * (GMAC descriptors + frame buffers; slave-private but DMA-coherent
     * by attribute, so the D-cache enable above needs no per-packet
     * maintenance in the driver). See gmac1_profile.h NET_DMA_*. */
    g_l2_low[L2_INDEX(NET_DMA_BASE)] = NC_PTE(NET_DMA_BASE);

    /* first 2MB split at the 1MB contract boundary: shared window is
     * Device, image head is Normal */
    g_l2_low[L2_INDEX(0x74000000UL)] = PTE_TABLE | (u64)(unsigned long)g_l3_shared;
    for (i = 0; i < 256; i++) {
        g_l3_shared[i] = DEVICE_PAGE(0x74000000UL + ((u64)i << 12));
    }
    for (i = 256; i < 512; i++) {
        g_l3_shared[i] = NORMAL_PAGE(0x74100000UL + ((u64)(i - 256) << 12));
    }
    /* peripherals: GIC-600 (GICD 0xFD400000 / GICR 0xFD580000) and
     * UART2 0xFE660000, covered as Device 2MB blocks */
    for (pa = 0xFD000000UL; pa < 0xFF000000UL; pa += 0x200000UL) {
        g_l2_high[L2_INDEX(pa)] = DEVICE_PTE(pa);
    }

    MmuEnable((u64)(unsigned long)g_l1);
}
