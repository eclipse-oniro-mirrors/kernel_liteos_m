/* ----------------------------------------------------------------------------
 * Copyright (c) 2026-2026. All rights reserved.
 * Description : Cache operations HeadFile (ws63 / RISC-V LinxCore131).
 *
 * This header is the single public entry point for all I/D-cache
 * maintenance on the ws63 RISC-V core. The implementation now ships as
 * open source in arch/risc-v/src/los_cache.c (gated by LOSCFG_ARCH_CACHE,
 * compiled into the arch module); the former closed prebuilt
 * liblos_cache.a has been removed.
 * ---------------------------------------------------------------------------- */

/**
 * @defgroup cache
 * @ingroup kernel
 */

#ifndef _ARCH_CACHE_H
#define _ARCH_CACHE_H

#include "los_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Cache line size (bytes) of the ws63 RISC-V LinxCore131 I/D cache. */
#define CACHE_LINE_SIZE         32U

typedef enum {
    CACHE_4KB  = 0,
    CACHE_8KB  = 2,
    CACHE_16KB = 4,
    CACHE_32KB = 6,
    CACHE_64KB = 8,
} CacheSize;

typedef enum {
    CACHE_PREF_1_LINES = 0,
    CACHE_PREF_2_LINES = 1,
    CACHE_PREF_3_LINES = 2,
    CACHE_PREF_4_LINES = 3,
    CACHE_PREF_5_LINES = 4,
    CACHE_PREF_6_LINES = 5,
    CACHE_PREF_7_LINES = 6,
    CACHE_PREF_8_LINES = 7,
    CACHE_PREF_LINES_MAX,
} CachePrefLines;

/* riscv cache register config (custom CSR space, ws63 LinxCore131).
 *
 * csr_bit[0]  ICEN   Instruction cache enable (1=enabled). Default disabled.
 * Bit[2:1]    ICS    Instruction cache size: 2b00=4KB, 2b01=8KB,
 *                    2b10=16KB, 2b11=32KB. Default 32KB.
 */
#define ICCTL                    0x7C0
#define ICCTL_ENABLE             0x1

/*
 * Bit[0]   DCEN  Data cache enable (1=enabled). Default disabled.
 * Bit[2:1] DCS   Data cache size: 2b00=4KB, 2b01=8KB, 2b10=16KB, 2b11=32KB.
 */
#define DCCTL                    0x7C1
#define DCCTL_ENABLE             0x1

/*
 * Bit[0]  VA    Instruction cache invalidation by all or VA: 1b0=all, 1b1=VA.
 * Bit[2]  ICIV  Initiate instruction cache invalidation when set to 1b1.
 *               When by VA, the virtual address is specified in ICINCVA CSR.
 */
#define ICMAINT                  0x7C2
#define DCMAINT                  0x7C3
#define ICINCVA                  0x7C4
#define DCINCVA                  0x7C5

#define VA                       0x1
#define ICIV                     (0x1U << 2)
#define DCIV                     (0x1U << 2)
#define DCC                       (0x1U << 3)

#define ICACHE_BY_ALL            ICIV
#define ICACHE_BY_VA             (ICIV | VA)

#define DCACHE_INV_BY_VA         (DCIV | VA)
#define DCACHE_INV_BY_ALL        DCIV
#define DCACHE_CLEAN_ALL         DCC
#define DCACHE_CLEAN_BY_VA       (DCC | VA)
#define DCACHE_FLUSH_BY_VA       (DCC | DCIV | VA)
#define DCACHE_FLUSH_ALL         (DCC | DCIV)

#define APREFI                   0x7C6
#define APREFD                   0x7C7
#define IAPEN                    0x1
#define SAPEN                    (0x1U << 4)

/**
 * @ingroup cache
 * @brief enable ICache.
 * @param  icclSize [IN] The Instruction cache size (one of CacheSize).
 * @retval #LOS_OK on success; non-zero on invalid size.
 */
UINT32 ArchICacheEnable(CacheSize icclSize);

/**
 * @ingroup cache
 * @brief enable DCache.
 * @param  dcclSize [IN] The Data cache size (one of CacheSize).
 * @retval #LOS_OK on success; non-zero on invalid size.
 */
UINT32 ArchDCacheEnable(CacheSize dcclSize);

/**
 * @ingroup cache
 * @brief Init instruction cache auto prefetch (ICL value 1-8).
 * @param  iclValue [IN] The Instruction cache auto prefetch value.
 * @retval #LOS_OK on success; non-zero on invalid value.
 */
UINT32 ArchICachePrefetchEnable(CachePrefLines iclValue);

/**
 * @ingroup cache
 * @brief Init data cache auto prefetch (ICL and SCL value 1-8).
 * @param  iclValue [IN] The Data cache auto prefetch value.
 * @param  sclValue [IN] The Data cache auto prefetch value.
 * @retval #LOS_OK on success; non-zero on invalid value.
 */
UINT32 ArchDCachePrefetchEnable(CachePrefLines iclValue, CachePrefLines sclValue);

/**
 * @ingroup cache
 * @brief flush DCache by Va.
 * @attention baseAddr is aligned down to CACHE_LINE_SIZE if not aligned.
 * @param  baseAddr [IN] The start address need flush.
 * @param  size     [IN] The size of flush memory.
 */
VOID ArchDCacheFlushByVa(UINTPTR baseAddr, UINT32 size);

/**
 * @ingroup cache
 * @brief flush ICache by Va.
 * @attention baseAddr is aligned down to CACHE_LINE_SIZE if not aligned.
 * @param  baseAddr [IN] The start address need flush.
 * @param  size     [IN] The size of flush memory.
 */
VOID ArchICacheFlushByVa(UINTPTR baseAddr, UINT32 size);

/**
 * @ingroup cache
 * @brief invalid DCache by Va.
 * @attention baseAddr is aligned down to CACHE_LINE_SIZE if not aligned.
 * @param  baseAddr [IN] The start address need flush.
 * @param  size     [IN] The size of flush memory.
 */
VOID ArchDCacheInvByVa(UINTPTR baseAddr, UINT32 size);

/**
 * @ingroup cache
 * @brief clean DCache by Va.
 * @attention baseAddr is aligned down to CACHE_LINE_SIZE if not aligned.
 * @param  baseAddr [IN] The start address need clean.
 * @param  size     [IN] The size of clean memory.
 */
VOID ArchDCacheCleanByVa(UINTPTR baseAddr, UINT32 size);

/**
 * @ingroup cache
 * @brief flush ICache by start/end address.
 * @attention startAddr/endAddr are aligned to CACHE_LINE_SIZE if not aligned.
 * @param  startAddr [IN] The start address need flush.
 * @param  endAddr   [IN] The end address need flush.
 */
VOID ArchICacheFlushByAddr(UINTPTR startAddr, UINTPTR endAddr);

/**
 * @ingroup cache
 * @brief flush DCache by start/end address.
 * @attention startAddr/endAddr are aligned to CACHE_LINE_SIZE if not aligned.
 * @param  startAddr [IN] The start address need flush.
 * @param  endAddr   [IN] The end address need flush.
 */
VOID ArchDCacheFlushByAddr(UINTPTR startAddr, UINTPTR endAddr);

/**
 * @ingroup cache
 * @brief clean DCache by start/end address.
 * @attention startAddr/endAddr are aligned to CACHE_LINE_SIZE if not aligned.
 * @param  startAddr [IN] The start address need clean.
 * @param  endAddr   [IN] The end address need clean.
 */
VOID ArchDCacheCleanByAddr(UINTPTR startAddr, UINTPTR endAddr);

/**
 * @ingroup cache
 * @brief invalidate DCache by start/end address.
 * @attention startAddr/endAddr are aligned to CACHE_LINE_SIZE if not aligned.
 * @param  startAddr [IN] The start address need invalidate.
 * @param  endAddr   [IN] The end address need invalidate.
 */
VOID ArchDCacheInvByAddr(UINTPTR startAddr, UINTPTR endAddr);

/**
 * @ingroup cache
 * @brief flush all ICache.
 */
VOID ArchICacheFlush(VOID);

/**
 * @ingroup cache
 * @brief flush all DCache.
 */
VOID ArchDCacheFlush(VOID);

/**
 * @ingroup cache
 * @brief invalidate all DCache.
 */
VOID ArchDCacheInvalidate(VOID);

#define flush_dcache(start, end) ArchDCacheFlushByAddr(start, end)
#define flush_icache() ArchICacheFlush()

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ARCH_CACHE_H */
