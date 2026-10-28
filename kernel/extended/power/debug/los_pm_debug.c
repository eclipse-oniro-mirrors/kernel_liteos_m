/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "los_pm_debug_pri.h"
#include "securec.h"
#include "los_debug.h"
#include "los_ringbuf.h"
#include "los_printf.h"
#include "los_tick.h"

static const char *g_sleepModeStr[] = {
    "Normal",
    "Light",
    "Deep",
    "Shutdown",
};

volatile BOOL g_lowpowerEnable = TRUE;

static char g_historyBuf[LOSCFG_PM_HISTORY_COUNT * sizeof(PmHistoryItem)];
static Ringbuf g_historyRing;

static PmHistoryItem g_curItem;

VOID OsPmDebugInit(VOID)
{
    (VOID)LOS_RingbufInit(&g_historyRing, g_historyBuf, sizeof(g_historyBuf), RBUF_OVERWRITE);
    g_curItem.sleepMode = (UINT8)LOS_SYS_NORMAL_SLEEP;
}

VOID OsPmDebugSleepStart(UINT8 mode)
{
    g_curItem.startTick = (UINT32)LOS_TickCountGet();
    g_curItem.sleepMode = mode;
    g_curItem.wakeIrq = 0;
}

VOID OsPmDebugSleepEnd(UINT32 wakeIrq)
{
    g_curItem.endTick = (UINT32)LOS_TickCountGet();
    g_curItem.wakeIrq = wakeIrq;
    (VOID)LOS_RingbufWrite(&g_historyRing, (const char *)&g_curItem, sizeof(PmHistoryItem));
}

VOID OsPmHistoryShow(VOID)
{
    PmHistoryItem item;
    UINT32 actualLen;
    UINT32 i;
    static char buf[LOSCFG_PM_HISTORY_COUNT * sizeof(PmHistoryItem)];

    PRINTK("------ PM Sleep History ------\n");
    PRINTK("#  Mode      Start    End      Ticks    WakeIRQ\n");

    (void)memset_s(buf, sizeof(buf), 0, sizeof(buf));
    actualLen = LOS_RingbufPeek(&g_historyRing, buf, sizeof(buf));
    for (i = 0; i + sizeof(PmHistoryItem) <= actualLen; i += sizeof(PmHistoryItem)) {
        (void)memcpy_s(&item, sizeof(item), &buf[i], sizeof(item));
        PRINTK("%-2u %-10s %-8u %-8u %-8u %u\n", i / sizeof(PmHistoryItem),
               (item.sleepMode < sizeof(g_sleepModeStr) / sizeof(g_sleepModeStr[0]))
                   ? g_sleepModeStr[item.sleepMode] : "?",
               item.startTick, item.endTick, item.endTick - item.startTick, item.wakeIrq);
    }
}

VOID LOS_EnableLowPower(VOID)
{
    g_lowpowerEnable = TRUE;
}

VOID LOS_DisableLowPower(VOID)
{
    g_lowpowerEnable = FALSE;
}

UINT32 LOS_GetLowPowerStatus(VOID)
{
    return (UINT32)g_lowpowerEnable;
}
