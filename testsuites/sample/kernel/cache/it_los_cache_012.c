/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND IN ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 用例名称: ItLosCache012
 * 用例描述: 缓存失效操作数据安全契约 + 全序列回归
 *
 * 验证 flush(回写+失效)/clean(回写)/invalidate(纯失效) 对脏数据的安全语义:
 *   - flush/clean 回写脏行到 SRAM, 失效后读回应得新值;
 *   - invalidate-by-VA/Addr 头尾非对齐时先 clean 邻居行, 失效后读回应得新值。
 *
 * 注意: ArchDCacheInvalidate() 全量纯失效(DCIV 不回写)会无差别丢弃所有脏 DCache
 * 行, 包括任务栈帧(ra/s0 保存值)。在任务运行中调用会踩坏栈导致崩溃, 故 A 段
 * 改为按地址失效, 不调全量 invalidate; F 段全量三连用 ArchDCacheFlush()(回写+
 * 失效, 安全)替代 ArchDCacheInvalidate()。
 */

#include "it_los_cache.h"
#include "los_printf.h"

#define CACHE78_OLD 0xA5A50000U
#define CACHE78_NEW 0xC0DE600DU

static volatile UINT32 g_cache78ProbeA = CACHE78_OLD;
static volatile UINT32 g_cache78ProbeB = CACHE78_OLD;
static volatile UINT32 g_cache78ProbeC = CACHE78_OLD;
static volatile UINT32 g_cache78ProbeD = CACHE78_OLD;

static UINT32 TestCase(VOID)
{
    UINT32 readBack;
    UINT32 defects = 0;

    (VOID)ArchICacheEnable(CACHE_32KB);
    (VOID)ArchDCacheEnable(CACHE_32KB);

    /* --- A: 按地址失效(原全量 invalidate 在任务运行中不安全, 改 by-VA) --- */
    ArchDCacheFlush();                    /* 缓存清空(干净) */
    g_cache78ProbeA = CACHE78_NEW;        /* 唯一脏行: 本探针变量 */
    ArchDCacheInvByVa((UINTPTR)&g_cache78ProbeA, sizeof(g_cache78ProbeA));
    readBack = g_cache78ProbeA;
    dprintf("[cache78]A inv-by-va: read 0x%08x(expect 0x%08x) %s\n",
            readBack, CACHE78_NEW, (readBack == CACHE78_NEW) ? "OK" : "WRITE-LOST");
    if (readBack != CACHE78_NEW) {
        defects++;
    }

    /* --- B: 按虚地址失效 --- */
    ArchDCacheFlush();
    g_cache78ProbeB = CACHE78_NEW;
    ArchDCacheInvByVa((UINTPTR)&g_cache78ProbeB, sizeof(g_cache78ProbeB));
    readBack = g_cache78ProbeB;
    dprintf("[cache78]B inv-by-va: read 0x%08x(expect 0x%08x) %s\n",
            readBack, CACHE78_NEW, (readBack == CACHE78_NEW) ? "OK" : "WRITE-LOST");
    if (readBack != CACHE78_NEW) {
        defects++;
    }

    /* --- C: 按物理地址失效 --- */
    ArchDCacheFlush();
    g_cache78ProbeC = CACHE78_NEW;
    ArchDCacheInvByAddr((UINTPTR)&g_cache78ProbeC, (UINTPTR)&g_cache78ProbeC + sizeof(g_cache78ProbeC));
    readBack = g_cache78ProbeC;
    dprintf("[cache78]C inv-by-addr: read 0x%08x(expect 0x%08x) %s\n",
            readBack, CACHE78_NEW, (readBack == CACHE78_NEW) ? "OK" : "WRITE-LOST");
    if (readBack != CACHE78_NEW) {
        defects++;
    }

    /* --- D: 全量清+失效(回写后失效, 安全) --- */
    ArchDCacheFlush();
    g_cache78ProbeD = CACHE78_NEW;
    ArchDCacheFlush();                    /* clean+inv: 应回写后失效 */
    readBack = g_cache78ProbeD;
    dprintf("[cache78]D flush-all: read 0x%08x(expect 0x%08x) %s\n",
            readBack, CACHE78_NEW, (readBack == CACHE78_NEW) ? "OK" : "WRITE-LOST");
    if (readBack != CACHE78_NEW) {
        defects++;
    }

    dprintf("[cache78] total write-lost ops: %u/4\n", defects);

    /* --- F: 全序列复刻循环(回归, 千次放大) ---
     * 逐条复刻 ItLosCache011 用例体(Enable/预取/栈缓冲/ByVa/ByAddr/
     * 全量 flush), 循环 1000 遍验证稳定性。
     * 注意: 不调 ArchDCacheInvalidate()(全量纯失效), 改用 ArchDCacheFlush()
     * (回写+失效) 避免踩坏任务栈。 */
    dprintf("[cache78]F seq-loop: enter (1000 passes)...\n");
    {
        volatile UINT32 fbuf[32];
        UINT32 it, k;
        for (it = 0; it < 1000U; it++) {
            (VOID)ArchICacheEnable(CACHE_32KB);
            (VOID)ArchDCacheEnable(CACHE_32KB);
            (VOID)ArchICachePrefetchEnable(CACHE_PREF_4_LINES);
            (VOID)ArchDCachePrefetchEnable(CACHE_PREF_4_LINES, CACHE_PREF_4_LINES);
            for (k = 0; k < 32; k++) {
                fbuf[k] = 0x30000U | k;
            }
            ArchDCacheFlushByVa((UINTPTR)fbuf, sizeof(fbuf));
            ArchDCacheCleanByVa((UINTPTR)fbuf, sizeof(fbuf));
            ArchDCacheInvByVa((UINTPTR)fbuf, sizeof(fbuf));
            ArchICacheFlushByVa((UINTPTR)fbuf, sizeof(fbuf));
            ArchICacheFlushByAddr((UINTPTR)fbuf, (UINTPTR)fbuf + sizeof(fbuf));
            ArchDCacheFlushByAddr((UINTPTR)fbuf, (UINTPTR)fbuf + sizeof(fbuf));
            ArchDCacheCleanByAddr((UINTPTR)fbuf, (UINTPTR)fbuf + sizeof(fbuf));
            ArchDCacheInvByAddr((UINTPTR)fbuf, (UINTPTR)fbuf + sizeof(fbuf));
            ArchICacheFlush();
            ArchDCacheFlush();
        }
        dprintf("[cache78]F seq-loop: SURVIVED %u passes\n", it);
    }
    ICUNIT_ASSERT_EQUAL(defects, 0, defects);
    return LOS_OK;
}

VOID ItLosCache012(VOID)
{
    TEST_ADD_CASE("ItLosCache012", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}
