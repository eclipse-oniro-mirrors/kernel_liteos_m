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
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 用例名称: ItLosCache012
 * 用例描述: #78 缺陷探针——缓存失效操作数据安全契约 + 全序列回归
 *
 * 【现象】ws63(上游 780d511ba+MR3 构建) ItLosCache011 全量维护序列执行中
 *   板静默冻结(裸单跑 3/3 必现, Test Start 后无输出, 无复位); 同用例在
 *   001/002/004/MAINTEST 构建通过(布局相位差异)。全量轮 009 区冻结系
 *   #58/#59 PM 后台任务, 与本缺陷无关。
 * 【根因】liblos_cache.a(二进制入库, 无源码) 全量维护操作(csrw ICMAINT/
 *   DCMAINT + fence, 反汇编实证无中断保护)与中断交互存在布局敏感死锁
 *   角落。数据面已证伪"失效丢脏行"(探针 A~D 四变体读回全部一致);
 *   IntLock 包裹三连可通过(对照实验 run6)。
 * 【修复方向】上游开源 liblos_cache.a 定界; 库侧全量维护序列加中断保护;
 *   测试侧全量序列外加 LOS_IntLock/LOS_IntRestore(实验验证有效)。
 * 【恢复方法】缺陷修复后置 bug_isolate.h 的
 *   KERNEL_BUG_13_CACHE_MAINT_PHASE_FREEZE=1, 本用例入自动套件:
 *   A~D 断言"失效操作不丢已写数据"修复后契约, F 千次全序列回归。
 *
 * 探测矩阵:
 *   A: ArchDCacheInvalidate()   全量纯失效(主缺陷面)
 *   B: ArchDCacheInvByVa()      按虚地址失效
 *   C: ArchDCacheInvByAddr()    按物理地址失效
 *   D: ArchDCacheFlush()        全量清+失效(反证对照)
 *   F: 011 全序列复刻循环 ×1000(修复后回归)
 *
 * 实测基线(2026-10-09, 证据见 question_artifacts/20261008_cache78/):
 *   A~D 全部读回一致(0/4 丢写)——失效操作对数据安全;
 *   裸 ItLosCache011 单跑冻结 3/3(run5), IntLock 后通过(run6)。
 */

/* 用例简要描述: 失效操作丢脏行必现探针(修复后契约断言) */
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

    /* --- A: 全量纯失效(主缺陷面) --- */
    ArchDCacheFlush();                    /* 缓存清空(干净) */
    g_cache78ProbeA = CACHE78_NEW;        /* 唯一脏行: 本探针变量 */
    ArchDCacheInvalidate();                /* 纯失效: 若丢脏行, SRAM 仍旧值 */
    readBack = g_cache78ProbeA;           /* 缓存空, 从 SRAM 读 */
    dprintf("[cache78]A whole-inv: read 0x%08x(expect 0x%08x) %s\n",
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

    /* --- D: 全量清+失效(反证对照, 应安全) --- */
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

    /* --- F: 011 全序列复刻循环(修复后回归, 千次放大) ---
     * 逐条复刻 ItLosCache011 用例体(Enable/预取/栈缓冲/ByVa/ByAddr/
     * 全量三连)循环 1000 遍: 缺陷存在时本段为冻结复现器(布局相位敏感);
     * 修复后应全部存活。 */
    dprintf("[cache78]F seq-loop: enter (1000 passes), may hang board if defect live...\n");
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
            ArchDCacheInvalidate();
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
