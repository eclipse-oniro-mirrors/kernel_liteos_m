/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * RK3588 board support: early board init (console).
 */

#include "los_typedef.h"
#include "uart.h"

VOID board_init(VOID)
{
    /* console first so early failures are visible */
    UartInit();

    /* MMU + caches are fully brought up by LosMmuInit()
     * (kernel/liteos_m/arch/arm64/los_mmu.c) from reset_vector.S before
     * main(): identity map, I-cache, and the D-cache for the whole Normal-WB
     * runtime space. Regions that must not cache carry their own attributes
     * there: the openamp shared window (0x74000000..0x740FFFFF) is
     * Device-mapped (vrings + net ctrl block, polled/DMAd by the Linux
     * master) and the net DMA pools (0x76000000..0x761FFFFF) are
     * Normal Non-Cacheable (GMAC is not IO-coherent). Nothing left here. */
}
