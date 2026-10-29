/*
 * Copyright (c) 2026-2026. All rights reserved.
 * Description: ws63 RISC-V (LinxCore131) I/D-cache maintenance source.
 *
 * Open-source replacement for the former closed liblos_cache.a. Mirrors the
 * fbb_ws63 libriscv.a cache.c.obj implementation: same custom CSR space
 * (0x7C0..0x7C7), same alignment helpers, same boundary-writeback logic in
 * ArchDCacheInvByVa, no interrupt protection (matches the official binary).
 */

#include "arch/cache.h"
#include "los_toolchain.h"
#include "los_builddef.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

STATIC INLINE UINTPTR CacheAddrAlignDown(UINTPTR addr)
{
    return addr & ~((UINTPTR)(CACHE_LINE_SIZE - 1));
}

STATIC INLINE UINTPTR CacheAddrAlignUp(UINTPTR addr)
{
    if (addr > (UINTPTR)(~(UINTPTR)(CACHE_LINE_SIZE - 1))) {
        return CacheAddrAlignDown(addr);
    }
    return CacheAddrAlignDown(addr + (CACHE_LINE_SIZE - 1));
}

LITE_OS_SEC_TEXT_MINOR STATIC VOID DCacheOperate(UINTPTR baseAddr, UINT32 size, UINT32 op)
{
    UINTPTR alignedBase = CacheAddrAlignDown(baseAddr);
    UINTPTR end = CacheAddrAlignUp(alignedBase + size);
    UINTPTR addr;
    for (addr = alignedBase; addr < end; addr += CACHE_LINE_SIZE) {
        __asm__ __volatile__("csrw 0x7c5, %0" :: "r"(addr) : "memory"); /* DCINCVA */
        __asm__ __volatile__("csrw 0x7c3, %0" :: "r"(op) : "memory");   /* DCMAINT */
    }
    __asm__ __volatile__("fence" ::: "memory");
}

LITE_OS_SEC_TEXT UINT32 ArchICacheEnable(CacheSize icclSize)
{
    if (((UINT32)icclSize & ~0x7U) != 0) {
        return LOS_NOK;
    }
    __asm__ __volatile__("csrw 0x7c0, %0" :: "r"((UINTPTR)icclSize | ICCTL_ENABLE) : "memory"); /* ICCTL */
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 ArchDCacheEnable(CacheSize dcclSize)
{
    if (((UINT32)dcclSize & ~0x7U) != 0) {
        return LOS_NOK;
    }
    __asm__ __volatile__("csrw 0x7c1, %0" :: "r"((UINTPTR)dcclSize | DCCTL_ENABLE) : "memory"); /* DCCTL */
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 ArchICachePrefetchEnable(CachePrefLines iclValue)
{
    if (iclValue > CACHE_PREF_8_LINES) {
        return LOS_NOK;
    }
    __asm__ __volatile__("csrw 0x7c6, %0" :: "r"((UINTPTR)iclValue << 1 | IAPEN) : "memory"); /* APREFI */
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 ArchDCachePrefetchEnable(CachePrefLines iclValue, CachePrefLines sclValue)
{
    if (iclValue > CACHE_PREF_8_LINES || sclValue > CACHE_PREF_8_LINES) {
        return LOS_NOK;
    }
    UINTPTR cfg = ((UINTPTR)sclValue << 5) | ((UINTPTR)iclValue << 1) | IAPEN | SAPEN;
    __asm__ __volatile__("csrw 0x7c7, %0" :: "r"(cfg) : "memory"); /* APREFD */
    return LOS_OK;
}

LITE_OS_SEC_TEXT VOID ArchDCacheFlushByVa(UINTPTR baseAddr, UINT32 size)
{
    DCacheOperate(baseAddr, size, DCACHE_FLUSH_BY_VA);
}

LITE_OS_SEC_TEXT VOID ArchDCacheCleanByVa(UINTPTR baseAddr, UINT32 size)
{
    DCacheOperate(baseAddr, size, DCACHE_CLEAN_BY_VA);
}

LITE_OS_SEC_TEXT VOID ArchICacheFlushByVa(UINTPTR baseAddr, UINT32 size)
{
    UINTPTR alignedBase = CacheAddrAlignDown(baseAddr);
    UINTPTR end = CacheAddrAlignUp(alignedBase + size);
    UINTPTR addr;
    for (addr = alignedBase; addr < end; addr += CACHE_LINE_SIZE) {
        __asm__ __volatile__("csrw 0x7c4, %0" :: "r"(addr) : "memory"); /* ICINCVA */
        __asm__ __volatile__("csrw 0x7c2, %0" :: "r"((UINTPTR)ICACHE_BY_VA) : "memory"); /* ICMAINT */
    }
    __asm__ __volatile__("fence" ::: "memory");
}

LITE_OS_SEC_TEXT VOID ArchDCacheInvByVa(UINTPTR baseAddr, UINT32 size)
{
    UINTPTR alignedBase = CacheAddrAlignDown(baseAddr);
    UINTPTR endUnaligned = baseAddr + size;
    UINTPTR endAligned = CacheAddrAlignDown(endUnaligned);
    /* Clean head/tail partial lines first: DCIV discards without writeback. */
    if (baseAddr != alignedBase) {
        DCacheOperate(alignedBase, CACHE_LINE_SIZE, DCACHE_CLEAN_BY_VA);
    }
    if (endUnaligned != endAligned) {
        DCacheOperate(endAligned, CACHE_LINE_SIZE, DCACHE_CLEAN_BY_VA);
    }
    DCacheOperate(baseAddr, size, DCACHE_INV_BY_VA);
}

LITE_OS_SEC_TEXT VOID ArchICacheFlushByAddr(UINTPTR startAddr, UINTPTR endAddr)
{
    UINTPTR alignedStart = CacheAddrAlignDown(startAddr);
    UINTPTR alignedEnd = CacheAddrAlignUp(endAddr);
    ArchICacheFlushByVa(alignedStart, (UINT32)(alignedEnd - alignedStart));
}

LITE_OS_SEC_TEXT VOID ArchDCacheFlushByAddr(UINTPTR startAddr, UINTPTR endAddr)
{
    UINTPTR alignedStart = CacheAddrAlignDown(startAddr);
    UINTPTR alignedEnd = CacheAddrAlignUp(endAddr);
    ArchDCacheFlushByVa(alignedStart, (UINT32)(alignedEnd - alignedStart));
}

LITE_OS_SEC_TEXT VOID ArchDCacheCleanByAddr(UINTPTR startAddr, UINTPTR endAddr)
{
    UINTPTR alignedStart = CacheAddrAlignDown(startAddr);
    UINTPTR alignedEnd = CacheAddrAlignUp(endAddr);
    ArchDCacheCleanByVa(alignedStart, (UINT32)(alignedEnd - alignedStart));
}

LITE_OS_SEC_TEXT VOID ArchDCacheInvByAddr(UINTPTR startAddr, UINTPTR endAddr)
{
    UINTPTR alignedStart = CacheAddrAlignDown(startAddr);
    UINTPTR alignedEnd = CacheAddrAlignUp(endAddr);
    ArchDCacheInvByVa(alignedStart, (UINT32)(alignedEnd - alignedStart));
}

LITE_OS_SEC_TEXT VOID ArchICacheFlush(VOID)
{
    __asm__ __volatile__("csrw 0x7c2, %0" :: "r"((UINTPTR)ICACHE_BY_ALL) : "memory"); /* ICMAINT */
    __asm__ __volatile__("fence" ::: "memory");
}

LITE_OS_SEC_TEXT VOID ArchDCacheFlush(VOID)
{
    __asm__ __volatile__("csrw 0x7c3, %0" :: "r"((UINTPTR)DCACHE_FLUSH_ALL) : "memory"); /* DCMAINT */
    __asm__ __volatile__("fence" ::: "memory");
}

LITE_OS_SEC_TEXT VOID ArchDCacheInvalidate(VOID)
{
    __asm__ __volatile__("csrw 0x7c3, %0" :: "r"((UINTPTR)DCACHE_INV_BY_ALL) : "memory"); /* DCMAINT */
    __asm__ __volatile__("fence" ::: "memory");
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
