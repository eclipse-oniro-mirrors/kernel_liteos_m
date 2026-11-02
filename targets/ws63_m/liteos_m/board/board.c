/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: WS63 board platform config (simplified from 2.08.5 board.c)
 *
 * Removed from 2.08.5: NMI handler, crashinfo, dma_cache, ArchBackTraceCustom,
 *   POSIX/stdio stubs, oal_get_sleep_ticks/oal_ticks_restore, BoardConfig.
 * Kept: LOS_SetSysClosk/LOS_GetSysClosk (clock getter/setter).
 */

#include "los_compiler.h"
#include "los_config.h"
#include "soc.h"

UINT32 gSysClock = OS_SYS_CLOCK;

VOID LOS_SetSysClosk(UINT32 clock)
{
    gSysClock = clock;
}

UINT32 LOS_GetSysClosk(VOID)
{
    return gSysClock;
}
