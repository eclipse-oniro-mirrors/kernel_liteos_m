/*
 * Copyright (c) 2026-2026. All rights reserved.
 * Description: RISC-V (LinxCore131/170) I/D-cache maintenance source.
 *
 * Unified implementation for LinxCore131 (ws63) and LinxCore170 (3322/DiTing).
 * Both cores share the same custom CSR space (0x7C0..0x7C7), alignment
 * helpers, and boundary-writeback logic. All multi-step maintenance
 * (DCacheOperate, ICacheFlushByVa, PrefetchEnable, full-cache Flush/Invalid)
 * uniformly uses LOS_IntLock/LOS_IntRestore for safety — this is a superset
 * of the 131 binary (no lock) and the 170 binary (with lock).
 *
 * Only Enable and full-invalidate symbol sets remain #ifdef-guarded because
 * 170 splits Enable into LinxM/NonLinxM and adds ArchICacheInvalid/InvAll.
 */

#include "arch/cache.h"
#include "los_toolchain.h"
#include "los_builddef.h"
#include "los_interrupt.h"

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
        UINT32 intSave = LOS_IntLock();
        __asm__ __volatile__("csrw 0x7c5, %0" :: "r"(addr) : "memory"); /* DCINCVA */
        __asm__ __volatile__("csrw 0x7c3, %0" :: "r"(op) : "memory");   /* DCMAINT */
        __asm__ __volatile__("fence" ::: "memory");
        LOS_IntRestore(intSave);
    }
}

/* ---- Enable (only divergent point: 170 splits LinxM/NonLinxM) ---- */
#ifdef LOSCFG_ARCH_LINX_M

LITE_OS_SEC_TEXT UINT32 ArchICacheEnableLinxM(CacheSize icclSize)
{
    (VOID)icclSize;
    UINT32 intSave = LOS_IntLock();
    __asm__ __volatile__("fence" ::: "memory");
    __asm__ __volatile__("csrw 0x7c0, %0" :: "r"((UINTPTR)1) : "memory"); /* ICCTL */
    __asm__ __volatile__("fence" ::: "memory");
    LOS_IntRestore(intSave);
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 ArchICacheEnableNonLinxM(CacheSize icclSize)
{
    if ((((UINT32)1 << icclSize) & 0x155U) == 0) {
        return LOS_NOK;
    }
    UINT32 intSave = LOS_IntLock();
    __asm__ __volatile__("fence" ::: "memory");
    __asm__ __volatile__("csrw 0x7c0, %0" :: "r"((UINTPTR)icclSize | ICCTL_ENABLE) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
    LOS_IntRestore(intSave);
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 ArchDCacheEnableLinxM(CacheSize dcclSize)
{
    (VOID)dcclSize;
    UINT32 intSave = LOS_IntLock();
    __asm__ __volatile__("fence" ::: "memory");
    __asm__ __volatile__("csrw 0x7c1, %0" :: "r"((UINTPTR)1) : "memory"); /* DCCTL */
    __asm__ __volatile__("fence" ::: "memory");
    LOS_IntRestore(intSave);
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 ArchDCacheEnableNonLinxM(CacheSize dcclSize)
{
    if ((((UINT32)1 << dcclSize) & 0x155U) == 0) {
        return LOS_NOK;
    }
    UINT32 intSave = LOS_IntLock();
    __asm__ __volatile__("fence" ::: "memory");
    __asm__ __volatile__("csrw 0x7c1, %0" :: "r"((UINTPTR)dcclSize | DCCTL_ENABLE) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
    LOS_IntRestore(intSave);
    return LOS_OK;
}

#else /* LinxCore131: single Enable, no LinxM/NonLinxM split */

LITE_OS_SEC_TEXT UINT32 ArchICacheEnable(CacheSize icclSize)
{
    if (((UINT32)icclSize & ~0x7U) != 0) {
        return LOS_NOK;
    }
    __asm__ __volatile__("csrw 0x7c0, %0" :: "r"((UINTPTR)icclSize | ICCTL_ENABLE) : "memory");
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 ArchDCacheEnable(CacheSize dcclSize)
{
    if (((UINT32)dcclSize & ~0x7U) != 0) {
        return LOS_NOK;
    }
    __asm__ __volatile__("csrw 0x7c1, %0" :: "r"((UINTPTR)dcclSize | DCCTL_ENABLE) : "memory");
    return LOS_OK;
}

#endif /* LOSCFG_ARCH_LINX_M */

/* ---- PrefetchEnable (unified: IntLock + fence for both cores) ---- */
LITE_OS_SEC_TEXT UINT32 ArchICachePrefetchEnable(CachePrefLines iclValue)
{
    if (iclValue > CACHE_PREF_8_LINES) {
        return LOS_NOK;
    }
    UINT32 intSave = LOS_IntLock();
    __asm__ __volatile__("csrw 0x7c6, %0" :: "r"((UINTPTR)iclValue << 1 | IAPEN) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
    LOS_IntRestore(intSave);
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 ArchDCachePrefetchEnable(CachePrefLines iclValue, CachePrefLines sclValue)
{
    if (iclValue > CACHE_PREF_8_LINES || sclValue > CACHE_PREF_8_LINES) {
        return LOS_NOK;
    }
    UINTPTR cfg = ((UINTPTR)sclValue << 5) | ((UINTPTR)iclValue << 1) | IAPEN | SAPEN;
    UINT32 intSave = LOS_IntLock();
    __asm__ __volatile__("csrw 0x7c7, %0" :: "r"(cfg) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
    LOS_IntRestore(intSave);
    return LOS_OK;
}

/* ---- ByVa (unified: IntLock per line in DCacheOperate and ICacheFlushByVa) ---- */
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
        UINT32 intSave = LOS_IntLock();
        __asm__ __volatile__("csrw 0x7c4, %0" :: "r"(addr) : "memory"); /* ICINCVA */
        __asm__ __volatile__("csrw 0x7c2, %0" :: "r"((UINTPTR)ICACHE_BY_VA) : "memory"); /* ICMAINT */
        __asm__ __volatile__("fence" ::: "memory");
        LOS_IntRestore(intSave);
    }
}

LITE_OS_SEC_TEXT VOID ArchDCacheInvByVa(UINTPTR baseAddr, UINT32 size)
{
    UINTPTR alignedBase = CacheAddrAlignDown(baseAddr);
    UINTPTR endUnaligned = baseAddr + size;
    UINTPTR endAligned = CacheAddrAlignDown(endUnaligned);
    if (baseAddr != alignedBase) {
        DCacheOperate(alignedBase, CACHE_LINE_SIZE, DCACHE_CLEAN_BY_VA);
    }
    if (endUnaligned != endAligned) {
        DCacheOperate(endAligned, CACHE_LINE_SIZE, DCACHE_CLEAN_BY_VA);
    }
    DCacheOperate(baseAddr, size, DCACHE_INV_BY_VA);
}

/* ---- ByAddr (unified: delegate to ByVa) ---- */
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

/* ---- Full-cache (unified: IntLock for Flush; symbol set divergent) ---- */
LITE_OS_SEC_TEXT VOID ArchICacheFlush(VOID)
{
    UINT32 intSave = LOS_IntLock();
    __asm__ __volatile__("fence" ::: "memory");
    __asm__ __volatile__("csrw 0x7c2, %0" :: "r"((UINTPTR)ICACHE_BY_ALL) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
    LOS_IntRestore(intSave);
}

LITE_OS_SEC_TEXT VOID ArchDCacheFlush(VOID)
{
    UINT32 intSave = LOS_IntLock();
    __asm__ __volatile__("fence" ::: "memory");
    __asm__ __volatile__("csrw 0x7c3, %0" :: "r"((UINTPTR)DCACHE_FLUSH_ALL) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
    LOS_IntRestore(intSave);
}

#ifdef LOSCFG_ARCH_LINX_M
/* 170: Invalid (with IntLock) + InvAll (without) for ICache/DCache */
LITE_OS_SEC_TEXT VOID ArchICacheInvalid(VOID)
{
    UINT32 intSave = LOS_IntLock();
    __asm__ __volatile__("fence" ::: "memory");
    __asm__ __volatile__("csrw 0x7c2, %0" :: "r"((UINTPTR)ICACHE_BY_ALL) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
    LOS_IntRestore(intSave);
}

LITE_OS_SEC_TEXT VOID ArchICacheInvAll(VOID)
{
    __asm__ __volatile__("csrw 0x7c2, %0" :: "r"((UINTPTR)ICACHE_BY_ALL) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
}

LITE_OS_SEC_TEXT VOID ArchDCacheInvalid(VOID)
{
    UINT32 intSave = LOS_IntLock();
    __asm__ __volatile__("fence" ::: "memory");
    __asm__ __volatile__("csrw 0x7c3, %0" :: "r"((UINTPTR)DCACHE_INV_BY_ALL) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
    LOS_IntRestore(intSave);
}

LITE_OS_SEC_TEXT VOID ArchDCacheInvAll(VOID)
{
    __asm__ __volatile__("csrw 0x7c3, %0" :: "r"((UINTPTR)DCACHE_INV_BY_ALL) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
}

#else /* LinxCore131: single ArchDCacheInvalidate (no IntLock, matches binary) */
LITE_OS_SEC_TEXT VOID ArchDCacheInvalidate(VOID)
{
    __asm__ __volatile__("csrw 0x7c3, %0" :: "r"((UINTPTR)DCACHE_INV_BY_ALL) : "memory");
    __asm__ __volatile__("fence" ::: "memory");
}
#endif /* LOSCFG_ARCH_LINX_M */

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
