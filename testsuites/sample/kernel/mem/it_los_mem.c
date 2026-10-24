/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_mem.h"

void *g_memPool = NULL;
void *g_testPool = NULL;

void MemStart(void)
{
    g_memPool = LOS_MemAlloc(LOSCFG_SYS_HEAP_ADDR, TEST_MEM_SIZE);
    g_testPool = LOS_MemAlloc(LOSCFG_SYS_HEAP_ADDR, TEST_POOL_SIZE);

    if ((g_memPool == NULL) || (g_testPool == NULL)) {
        PRINT_ERR("alloc failed , mem TestCase would be failed!!!!%p !!! %p", g_memPool, g_testPool);
    }
}

void MemEnd(void)
{
    (VOID)LOS_MemFree(LOSCFG_SYS_HEAP_ADDR, g_memPool);
    (VOID)LOS_MemFree(LOSCFG_SYS_HEAP_ADDR, g_testPool);
    g_memPool = NULL;
    g_testPool = NULL;
}

void MemInit(void)
{
    (void)LOS_MemInit(g_memPool, TEST_MEM_SIZE);
}

void MemFree(void)
{
#if (LOSCFG_MEM_MUL_POOL == 1)
    (void)LOS_MemDeInit(g_memPool);
#endif
}

UINT32 MemGetFreeSize(void *pool)
{
    return LOS_MemPoolSizeGet(pool) - LOS_MemTotalUsedGet(pool);
}

UINT32 CalPow(UINT32 exp)
{
    UINT32 pw = 1;
    pw <<= exp;
    return pw;
}

#ifdef LOSCFG_MEM_MUL_POOL_ALLOC
/* mulpool 测试池缓冲改按需动态申请(32 对齐, 与原静态 aligned(32) 语义一致):
 * 原 32KB 静态池常驻 .bss 挤占 SRAM 预算(覆盖率构建下 gcov 计数器已占大量
 * .bss, 实测堆余量不足以支撑 pthread 8KB 线程栈)。生命周期由内核回调托管:
 * OsMulPoolExpand 经 allocMemPoolFunc 取缓冲(los_multipool.c:194, 返回 0 走
 * 回退), 池 DeInit 完成后经 freeMemPoolFunc 归还(los_multipool.c:330),
 * 任意顺序释放均按指针匹配槽位, 套件外零占用。 */
VOID *g_poolMem[TEST_POOL_MAX_NUM] = {NULL};
UINT32 g_poolNum;

VOID TestGetAddrSize(UINTPTR *poolAddr, UINT32 *poolSize)
{
    UINT32 i;

    for (i = 0; i < TEST_POOL_MAX_NUM; i++) {
        if (g_poolMem[i] == NULL) {
            g_poolMem[i] = LOS_MemAllocAlign(LOSCFG_SYS_HEAP_ADDR, TEST_EACH_POOL_SIZE, 32);
            if (g_poolMem[i] == NULL) {
                break; /* 申请失败: 以下返回 0, 内核走回退 */
            }
            *poolAddr = (UINTPTR)g_poolMem[i];
            *poolSize = TEST_EACH_POOL_SIZE;
            g_poolNum++;
            return;
        }
    }
    *poolAddr = 0;
    *poolSize = 0;
}

VOID TestReleaseAddrSize(VOID *pool)
{
    UINT32 i;

    for (i = 0; i < TEST_POOL_MAX_NUM; i++) {
        if (g_poolMem[i] == pool) {
            (VOID)LOS_MemFree(LOSCFG_SYS_HEAP_ADDR, pool);
            g_poolMem[i] = NULL;
            break;
        }
    }
    if (g_poolNum > 0) {
        g_poolNum--;
    }
}

VOID TEST_MulPoolUnRegister(VOID)
{
    extern UINT32 g_poolFlag;
    extern UINT32 g_mulPoolMux;
    UINT32 i;

    /* 补偿 OsMulPoolShrink 对 slab 池的静默跳过：扩展池经 LOS_MemInit 创建
     * （bestfit 硬编码 SLAB 属性，池 init 即划走 slab 类 = 永久 used 节点），
     * OsCheckPoolNoUsed 恒 FALSE，shrink 无法移除链上扩展池、钩子缓冲永不
     * 归还。注销时按槽位 DeInit 摘链并显式归还动态缓冲，保证每个用例结束
     * 后链与槽位恢复干净，后续用例的扩展落槽可确定性预期（与 mulpool_005
     * 入口清理同款；DeInit 不回调 freeMemPoolFunc，须显式归还）。 */
    for (i = 0; i < TEST_POOL_MAX_NUM; i++) {
        if (g_poolMem[i] != NULL) {
            if (LOS_MemDeInit(g_poolMem[i]) == LOS_OK) {
                TestReleaseAddrSize(g_poolMem[i]);
            }
        }
    }

    (VOID)LOS_MuxDelete(g_mulPoolMux);
    g_poolFlag = 0;
    g_mulPoolMux = 0;
}
#endif

VOID ItSuiteLosMem(void)
{
    MemStart();

    ItLosMem001();
    ItLosMem002();
    ItLosMem003();
#ifdef LOSCFG_KERNEL_MEM_TLSF
    ItLosMem004();
#endif
    ItLosMem006();
    ItLosMem007();
    ItLosMem008();
    ItLosMem009();
    ItLosMem010();
    ItLosMem011();
#ifndef LOSCFG_MEM_DEBUG
    ItLosMem012();
    ItLosMem013();
#endif
#if !defined(LOSCFG_MEM_HEAD_BACKUP) && !defined(LOSCFG_MEM_TASK_STAT)
    /* When LOSCFG_MEM_HEAD_BACKUP is enabled, the TEST_MEM_SIZE is not enough. */
    ItLosMem014();
#endif
    ItLosMem015();
    ItLosMem016();
    ItLosMem017();
#ifndef LOSCFG_KERNEL_MEM_BESTFIT_LITTLE
    /*
     * bestfit_little's LOS_MemRealloc always moves (alloc+copy+free), it does
     * not support in-place shrink, so these in-place-realloc cases are skipped.
     */
    ItLosMem018();
    ItLosMem019();
    ItLosMem020();
#endif
#if (LOSCFG_TEST_MUCH_LOG == 1) // when open this， Too many logs will printed
    ItLosMem035();
    ItLosMem036();
    ItLosMem037();
    ItLosMem038();
#endif
#ifdef LOSCFG_KERNEL_MEM_TLSF
    ItLosMem040();
#endif
    ItLosMem045();
#if (LOSCFG_MEM_MUL_REGIONS == 1)
    ItLosMem046();
    ItLosMem047();
#endif

    /* Common memory API tests (work on both TLSF and bestfit) */
    ItLosMem048(); /* LOS_MemInfoGet */
#if defined(LOSCFG_KERNEL_MEM_TLSF) || defined(LOSCFG_KERNEL_MEM_BESTFIT)
    ItLosMem049(); /* LOS_MemFreeNodeShow */
#endif
#if (LOSCFG_MEM_MUL_POOL == 1)
    ItLosMem050(); /* LOS_MemPoolList + LOS_MemDeInit */
#endif
#ifdef LOSCFG_KERNEL_MEM_TLSF
    ItLosMem051(); /* LOS_MemUnlockEnable (TLSF only) */
#if (LOSCFG_MEM_LEAKCHECK == 1)
    ItLosMem052(); /* LOS_MemUsedNodeShow */
    ItLosMem318(); /* LOS_MemUsedNodeShow NULL pool early-return (TLSF+LEAKCHECK only) */
#endif
    ItLosMem319(); /* LOS_MemFreeNodeShow: free-list middle-node delete + small-bucket print */
    ItLosMem320(); /* LOS_MemAllocAlign/Free fuzz: bad gap word (TLSF only) */
    ItLosMem321(); /* LOS_MemFree fuzz: free-node ptr.prev out of range (TLSF only) */
#endif
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    ItLosMem053(); /* LOS_MemPoolInit */
    ItLosMem054(); /* LOS_MemFreeBlksGet + LOS_MemUsedBlksGet */
    ItLosMem055(); /* LOS_MemTaskIdGet + LOS_MemLastUsedGet */
#endif
#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (LOSCFG_BASE_MEM_NODE_SIZE_CHECK == 1)
    ItLosMem056(); /* LOS_MemNodeSizeCheck + LOS_MemCheckLevelSet/Get */
#endif
#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (LOSCFG_MEM_MUL_MODULE == 1)
    ItLosMem057(); /* LOS_MemMalloc/MallocAlign/Mfree/Mrealloc/MusedGet */
#endif
#if (LOSCFG_KERNEL_MEM_SLAB_EXTENTION == 1)
    ItLosMem322(); /* LOS_SlabSizeCfg: cross-32bit-word bitmap, 33 allocs (slab) */
    ItLosMem323(); /* LOS_SlabSizeCfg: slab class alloc failure -> bestfit fallback (slab) */
#endif
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    ItLosMem324(); /* LOS_MemPoolInit: unaligned size warning + aligned down (bestfit) */
    ItLosMem325(); /* g_MALLOC_HOOK: hook fired once per alloc (bestfit) */
#endif
#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (LOSCFG_MEM_MUL_MODULE == 1)
    ItLosMem326(); /* LOS_MemMalloc/Mfree fuzz: bad gap word (bestfit+mulmodule) */
#endif
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    ItLosMem327(); /* LOS_MemAllocAlign/Free fuzz: used|aligned gap word (bestfit) */
#endif
#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (defined(LOSCFG_MEM_DEBUG) || defined(LOSCFG_MEM_TASK_STAT))
    ItLosMem328(); /* LOS_MemInfoGet/PoolList fuzz: END-node magic broken (bestfit) */
#endif
#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (LOSCFG_BASE_MEM_NODE_INTEGRITY_CHECK == 1)
    ItLosMem329(); /* LOS_MemIntegrityCheck fuzz: dlink head pstPrev broken (bestfit) */
#endif
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    ItLosMem330(); /* LOS_MemTaskIdGet: freed-node + END-address miss branches (bestfit) */
    ItLosMem331(); /* LOS_MemLastUsedGet: used-last-neighbor exact value (bestfit) */
#endif
#if (LOSCFG_KERNEL_MEM_TLSF == 1) && (LOSCFG_MEM_FREE_BY_TASKID == 1)
    ItLosMem058(); /* LOS_MemFreeByTaskID */
#endif

#if (LOS_KERNEL_TEST_FULL == 1)
    ItLosTick001();
    ItLosTick002();
    ItLosTick003();
    ItLosTick004();
    ItLosTick005();
    ItLosTick006();
    ItLosTick007();
    ItLosTick008();
    ItLosTick009();
    ItLosTick010();
    ItLosTick011();
    ItLosTick012();
    ItLosTick013();
#if defined(FUZZ_BUG_S06_OSEVFLAG_DBLDEL) && FUZZ_BUG_S06_OSEVFLAG_DBLDEL == 1
    /* BUG 宏见 bug_isolate.h: 修复前隔离(不注册不执行), 修复后置 1 恢复 */
    ItLosTick014();
#endif
    ItLosTick015(); /* LOS_SleepTicksGet 安全版(规避问题13的延时态任务删除) */
    ItLosTick016(); /* OsCpuTick2MS/OsCpuTick2US NULL 拒绝 + 换算契约 */
    ItLosTick017(); /* LOS_TickTimerRegister 参数校验分支 */
    ItLosMem301();
    ItLosMem310(); /* LOS_Membox Alloc/Free lifecycle + StatisticsGet (membox) */
    ItLosMem311(); /* LOS_MemboxClr (membox) */
    ItLosMem312(); /* SLAB_MSK pool via LOS_MemPoolInit (slab extension) */
    ItLosMem313(); /* bestfit_little defensive behaviors (bestfit_little only) */
    ItLosMem314(); /* bestfit_little LOS_MemRealloc boundaries (bestfit_little only) */
    ItLosMem316(); /* MemInit/PoolInit 校验分支 + Show/Debug 接口(tlsf/bestfit) */
    ItLosMem317(); /* bestfit TaskIdGet/NodeSizeCheck/Mrealloc/Mfree 分支 */
#endif
#if (LOS_KERNEL_TEST_MANUAL == 1) && defined(LOSCFG_KERNEL_MEM_BESTFIT)
    /* DESTRUCTIVE/NON-RETURNING: heap corruption dump chain, manual only */
    ItLosMem315();
#endif
#ifdef LOSCFG_MEM_MUL_POOL_ALLOC
    ItLosMem332(); /* LOS_MulPool* unregistered-hook rejects: MUST run before ItLosMulPool001
                      (g_poolFlag is irreversible once registered, los_multipool.c:165-184) */
    ItLosMulPool001();
    ItLosMulPool002();
    ItLosMulPool003(); /* LOS_MulPoolRealloc/LOS_MulPoolFree 分支 */
    ItLosMulPool004(); /* LOS_MulPoolRealloc 跨池拷贝(OsMulPoolCopyToNewNode) */
    ItLosMulPool005(); /* LOS_MulPoolShrink 有占用池跳过 */
#endif
    /* BUG_24(resleak 批次): OsSlabStatisticsGet 孤儿零调用——slab 内部泄漏
     * 不可观测(红灯暴露用例); 仅 slab 板(如 ws63/hi3322)注册 */
#if defined(LOSCFG_KERNEL_MEM_SLAB_EXTENTION)
#if defined(KERNEL_BUG_24_SLAB_STAT_BLIND) && KERNEL_BUG_24_SLAB_STAT_BLIND == 1
    ItLosMem333();
#endif
#endif
    MemEnd();
}
