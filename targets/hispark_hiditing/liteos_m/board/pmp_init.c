/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 PMP initialization — adapted from hs-fbb pmp_config.c.
 *
 * Sets up PMP entries 18-31 for the app (REE) memory regions, mirroring
 * hs-fbb's pmp_config.c. Entries 0-15 are managed by SSB/seliteos (TEE).
 *
 * Hi3322 PMP CSR layout:
 *   pmpcfg0-7:   0x3A0-0x3A7  (4 entries per 32-bit register)
 *   pmpaddr0-15: 0x3B0-0x3BF  (standard RISC-V)
 *   pmpaddr16-31: 0x3C0-0x3CF (custom)
 *   memattr0-15: 0xBC0-0xBCF  (custom)
 *   memattr16-31: 0xBD0-0xBDF (custom)
 *
 * pmpcfg byte format (Hi3322 custom):
 *   bit 7:   L (locked)
 *   bits 6:5: reserved
 *   bits 4:3: A (0=off, 1=TOR, 2=NA4, 3=NAPOT)
 *   bit 2:   X (execute)
 *   bit 1:   W (write)
 *   bit 0:   R (read)
 *
 * NAPOT pmpaddr = (base_addr + (size / 2) - 1) >> 2
 */

#include "los_compiler.h"
#include "pmp_init.h"

/* pmpcfg byte: L=1, A=NAPOT(3), rwx */
#define PMPCFG_NAPOT_LOCKED(rwx)  (0x80 | (0x3 << 3) | (rwx))

#define RWX_RW             0x3
#define RWX_RX             0x5

#define MEM_ATTR_DEVICE              0
#define MEM_ATTR_NON_CACHEABLE       2
#define MEM_ATTR_WB_RALLOCATE        7

#define MEMATTR_LOW4_MASK            0x0Fu
#define MEMATTR_LOW4_CLEAR_MASK      0xFFFFFFF0u

/* CSR addresses */
#define CSR_PMPCFG4    0x3A4
#define CSR_PMPCFG5    0x3A5
#define CSR_PMPCFG6    0x3A6
#define CSR_PMPCFG7    0x3A7

#define CSR_PMPADDR18  0x3C2
#define CSR_PMPADDR19  0x3C3
#define CSR_PMPADDR22  0x3C6
#define CSR_PMPADDR23  0x3C7
#define CSR_PMPADDR26  0x3CA
#define CSR_PMPADDR27  0x3CB
#define CSR_PMPADDR28  0x3CC
#define CSR_PMPADDR29  0x3CD
#define CSR_PMPADDR30  0x3CE
#define CSR_PMPADDR31  0x3CF

#define CSR_MEMATTR18  0xBD2
#define CSR_MEMATTR19  0xBD3
#define CSR_MEMATTR22  0xBD6
#define CSR_MEMATTR23  0xBD7
#define CSR_MEMATTR26  0xBDA
#define CSR_MEMATTR27  0xBDB
#define CSR_MEMATTR28  0xBDC
#define CSR_MEMATTR29  0xBDD
#define CSR_MEMATTR30  0xBDE
#define CSR_MEMATTR31  0xBDF

static inline __attribute__((always_inline)) void write_csr_num(uint32_t csr, uint32_t val)
{
    __asm__ volatile("csrw %0, %1" :: "i"(csr), "r"(val));
}

static inline __attribute__((always_inline)) uint32_t read_csr_num(uint32_t csr)
{
    uint32_t val;
    __asm__ volatile("csrr %0, %1" : "=r"(val) : "i"(csr));
    return val;
}

static inline __attribute__((always_inline)) uint32_t napot_addr(uint32_t base, uint32_t size)
{
    return (base + (size >> 1) - 1) >> 2;
}

static inline __attribute__((always_inline)) void set_memattr(uint32_t memattr_csr, uint8_t memattr)
{
    /* hs-fbb pmp_set_mem_attr: preserve upper 28 bits, only set low 4.
     * The upper bits include the auto-set "valid/M-mode access" flag
     * (bit 16, observed set in all entries after init). Writing just
     * the low 4 bits keeps that flag intact. */
    uint32_t v = read_csr_num(memattr_csr);
    v = (v & MEMATTR_LOW4_CLEAR_MASK) | (memattr & MEMATTR_LOW4_MASK);
    write_csr_num(memattr_csr, v);
}

static inline __attribute__((always_inline)) void set_pmp_entry(uint32_t pmpaddr_csr, uint32_t memattr_csr,
                          uint32_t pmpcfg_csr, uint32_t entry_in_reg,
                          uint32_t base_addr, uint32_t size,
                          uint8_t memattr, uint8_t rwx)
{
    uint32_t addr = napot_addr(base_addr, size);
    uint8_t cfg = PMPCFG_NAPOT_LOCKED(rwx);

    write_csr_num(pmpaddr_csr, addr);
    set_memattr(memattr_csr, memattr);

    uint32_t shift = entry_in_reg * 8;
    uint32_t mask = 0xFF << shift;
    uint32_t reg = read_csr_num(pmpcfg_csr);
    reg = (reg & ~mask) | ((uint32_t)cfg << shift);
    write_csr_num(pmpcfg_csr, reg);
}

void pmp_init(void)
{
    /* Mirror hs-fbb's src/drivers/chips/3322/config/acore/normal/pmp_config.c
     * region table (REE app's pmp_init). hs-fbb configures entries 17-31
     * (skipping 16 which is reserved for SSB's ITCM startup vector, and 20/21
     * which are left at default). Entries 17/24/25 (PSRAM) are omitted
     * since hi3322 doesn't use PSRAM. All other entries match hs-fbb. */

    /* Entry 18: norflash 32MB @ 0x26000000, cacheable, R+X */
    set_pmp_entry(CSR_PMPADDR18, CSR_MEMATTR18, CSR_PMPCFG4, 2,
                  0x26000000, 0x02000000, MEM_ATTR_WB_RALLOCATE, RWX_RX);

    /* Entry 22: itcm + bootrom 512KB @ 0x0, non-cacheable, R+X */
    set_pmp_entry(CSR_PMPADDR22, CSR_MEMATTR22, CSR_PMPCFG5, 2,
                  0x00000000, 0x00080000, MEM_ATTR_NON_CACHEABLE, RWX_RX);

    /* Entry 23: dtcm 1MB @ 0x10000000, non-cacheable, R+W (the heap,
     * task stacks, etc. all live here — 0x10010000-0x1009FFFF) */
    set_pmp_entry(CSR_PMPADDR23, CSR_MEMATTR23, CSR_PMPCFG5, 3,
                  0x10000000, 0x00100000, MEM_ATTR_NON_CACHEABLE, RWX_RW);

    /* Entry 26: SFC control registers 8KB @ 0x28000000, device, R+W */
    set_pmp_entry(CSR_PMPADDR26, CSR_MEMATTR26, CSR_PMPCFG6, 2,
                  0x28000000, 0x00002000, MEM_ATTR_DEVICE, RWX_RW);

    /* Entry 27: L2M 4MB @ 0x20000000, non-cacheable, R+W */
    set_pmp_entry(CSR_PMPADDR27, CSR_MEMATTR27, CSR_PMPCFG6, 3,
                  0x20000000, 0x00400000, MEM_ATTR_NON_CACHEABLE, RWX_RW);

    /* Entry 28: soc registers 256MB @ 0x50000000, device, R+W */
    set_pmp_entry(CSR_PMPADDR28, CSR_MEMATTR28, CSR_PMPCFG7, 0,
                  0x50000000, 0x10000000, MEM_ATTR_DEVICE, RWX_RW);

    /* Entry 29: sharedram 64MB @ 0x80000000, non-cacheable, R+W */
    set_pmp_entry(CSR_PMPADDR29, CSR_MEMATTR29, CSR_PMPCFG7, 1,
                  0x80000000, 0x04000000, MEM_ATTR_NON_CACHEABLE, RWX_RW);

    /* Entry 30: CLIC registers 256MB @ 0xB0000000, device, R+W */
    set_pmp_entry(CSR_PMPADDR30, CSR_MEMATTR30, CSR_PMPCFG7, 2,
                  0xB0000000, 0x10000000, MEM_ATTR_DEVICE, RWX_RW);

    /* Entry 31: 16MB "background" RW region @ 0x22000000, non-cacheable.
     * Overwrites SSB's stale 0x9D (RX Locked) that could silently deny
     * writes if it overlapped any DTCM address. */
    set_pmp_entry(CSR_PMPADDR31, CSR_MEMATTR31, CSR_PMPCFG7, 3,
                  0x22000000, 0x01000000, MEM_ATTR_NON_CACHEABLE, RWX_RW);

    /* Entry 19: dynload DSO pool, ITCM 64KB @ 0x40000; must stay below entry 22. */
    set_pmp_entry(CSR_PMPADDR19, CSR_MEMATTR19, CSR_PMPCFG4, 3,
                  HI3322_DYNLOAD_POOL_BASE, HI3322_DYNLOAD_POOL_SIZE,
                  MEM_ATTR_NON_CACHEABLE, RWX_RW);

    __asm__ volatile("fence" ::: "memory");
}

/* Loader hooks: flip entry 19 RW while loading, RX while loaded (W^X). */
UINT32 ArchDynloadPrepare(VOID)
{
    set_pmp_entry(CSR_PMPADDR19, CSR_MEMATTR19, CSR_PMPCFG4, 3,
                  HI3322_DYNLOAD_POOL_BASE, HI3322_DYNLOAD_POOL_SIZE,
                  MEM_ATTR_NON_CACHEABLE, RWX_RW);
    __asm__ volatile("fence" ::: "memory");
    return 0; /* LOS_OK */
}

UINT32 ArchDynloadFinish(UINTPTR codeAddrStart, UINTPTR codeAddrEnd,
                         UINTPTR dataAddrStart, UINTPTR dataAddrEnd)
{
    (VOID)codeAddrStart;
    (VOID)codeAddrEnd;
    (VOID)dataAddrStart;
    (VOID)dataAddrEnd;
    set_pmp_entry(CSR_PMPADDR19, CSR_MEMATTR19, CSR_PMPCFG4, 3,
                  HI3322_DYNLOAD_POOL_BASE, HI3322_DYNLOAD_POOL_SIZE,
                  MEM_ATTR_NON_CACHEABLE, RWX_RX);
    __asm__ volatile("fence" ::: "memory");
    return 0; /* LOS_OK */
}
