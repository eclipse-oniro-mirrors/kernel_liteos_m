#include "los_hwi.h"

#if defined(LOSCFG_ARCH_CORTEX_M7) || defined(LOSCFG_ARCH_CORTEX_M55)
#define CACHE_REG32_WRITE(addr, val)    (*(volatile UINT32 *)(UINTPTR)(addr) = (val))
#endif

VOID ArchCleanDCacheByAddress(UINTPTR addr, UINT32 len)
{
#if defined(LOSCFG_ARCH_CORTEX_M7) || defined(LOSCFG_ARCH_CORTEX_M55)
    UINT32 linesize;
    UINT32 align;
    UINT32 start, end;
    UINT32 tmp;

    linesize = LINE_SIZE;
    align = linesize - 1;
    start = (UINT32)addr;
    end = (UINT32)addr + len;
    start = start & (~align);
    for (tmp = start; tmp < end; tmp = tmp + linesize) {
        CACHE_REG32_WRITE(CACHE_DCCMVAC, tmp);
    }
    DSB();
    ISB();
#else
    (VOID)addr;
    (VOID)len;
#endif
}

LITE_OS_SEC_TEXT VOID ArchDCacheFlushByAddr(UINTPTR startAddr, UINTPTR endAddr)
{
#if defined(LOSCFG_ARCH_CORTEX_M7) || defined(LOSCFG_ARCH_CORTEX_M55)
    UINT32 size;

    UINTPTR start = (UINT32)startAddr & ~(LINE_SIZE - 1);
    UINTPTR end = LOS_Align(endAddr, LINE_SIZE);
    size = end - start;

    ArchCleanDCacheByAddress(startAddr, size);
#else
    (VOID)startAddr;
    (VOID)endAddr;
#endif    
}

LITE_OS_SEC_TEXT VOID ArchICacheFlush(VOID)
{
#if defined(LOSCFG_ARCH_CORTEX_M7) || defined(LOSCFG_ARCH_CORTEX_M55)
    DSB();
    ISB();
    CACHE_REG32_WRITE(CACHE_ICIALLU, 0UL);
    DSB();
    ISB();
#endif
}