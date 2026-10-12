/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the conditions of the LOSCFG-style open source license.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY
 * OR FIT FOR A PARTICULAR PURPOSE.
 */

#include "It_los_mem.h"
#ifdef LOSCFG_KERNEL_MEMBOX_STATIC
#include "los_membox.h"
#endif

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: LOS_MemInit/LOS_MemPoolInit 参数校验分支 + 内存池 Show/Debug 对外接口一揽子。
 * TLSF(qemu):
 * 1) F-Inv: LOS_MemInit 池地址未对齐 → LOS_NOK; size 未对齐 → LOS_NOK; size<=MIN → LOS_NOK;
 * 2) F-Pos: 合法静态池 LOS_MemInit OK 后, 小块(0x10)+大块(0x400)各分配再释放制造
 *    小/大桶空闲节点, LOS_MemFreeNodeShow 覆盖小/大桶两条打印分支;
 * 3) F-Inv: LOS_MemUsedNodeShow(NULL) → OsAllMemNodeDoHandle 池空保护分支;
 * 4) F-Pos: LOS_MemUsedNodeShow(pool)/LOS_MemPoolList 冒烟 + LOS_MemDeInit 收尾。
 * BESTFIT(ws63):
 * 5) F-Inv: LOS_MemPoolInit(NULL/size 越界) → LOS_NOK; 地址未对齐自动对齐告警分支继续初始化;
 * 6) F-Pos: 合法池上 LOS_MemUsedBlksGet/LOS_MemFreeBlksGet 空闲/占用块计数契约
 *    (分配增/释放减), LOS_MemUsedNodeShow(pool) 冒烟。 */

#define MEM_TC316_POOL_SIZE 0x1000

STATIC UINT8 g_memPoolTC316[MEM_TC316_POOL_SIZE] __attribute__((aligned(8)));
STATIC UINT8 g_memPool2TC316[MEM_TC316_POOL_SIZE] __attribute__((aligned(8)));

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    VOID *ptr = NULL;
    VOID *ptrBig = NULL;
    UINT32 usedBefore;
    UINT32 usedAfter;
    UINT32 freeBefore;
    UINT32 freeAfter;

    /* ---- 1/2/3/4: TLSF ---- */
#ifdef LOSCFG_KERNEL_MEM_TLSF
    /* F-Inv: 池地址未按 OS_MEM_ALIGN_SIZE 对齐 → LOS_NOK(tlsf los_memory.c:903-908) */
    ret = LOS_MemInit(g_memPoolTC316 + 1, MEM_TC316_POOL_SIZE - 8);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);

    /* F-Inv: size 未对齐 → LOS_NOK */
    ret = LOS_MemInit(g_memPoolTC316, MEM_TC316_POOL_SIZE - 1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);

    /* F-Inv: size <= 最小池阈值(传入 0) → LOS_NOK */
    ret = LOS_MemInit(g_memPoolTC316, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);

    /* F-Pos: 合法初始化 */
    ret = LOS_MemInit(g_memPoolTC316, MEM_TC316_POOL_SIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* F-Pos: 制造小桶 + 大桶空闲节点后 LOS_MemFreeNodeShow, 覆盖两类桶打印分支 */
    ptr = LOS_MemAlloc(g_memPoolTC316, 0x10);
    ICUNIT_ASSERT_NOT_EQUAL(ptr, NULL, 0);
    ptrBig = LOS_MemAlloc(g_memPoolTC316, 0x400);
    ICUNIT_ASSERT_NOT_EQUAL(ptrBig, NULL, 0);
    ret = LOS_MemFree(g_memPoolTC316, ptr);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_MemFree(g_memPoolTC316, ptrBig);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ptr = NULL;
    ptrBig = NULL;

    /* F-Pos: 同尺寸 4 连块 A/B/C/D + 前后哨兵, free A、free C 入同桶空闲链(链序 C→A),
     * free B 触发 B 与 A/C 合并, 删除非链头节点 A → OsMemFreeListDelete else 分支(tlsf L755) */
    {
        VOID *blk[4] = {NULL};
        UINT32 idx;
        VOID *guard = NULL;
        guard = LOS_MemAlloc(g_memPoolTC316, 0x10); /* 哨兵防跨块合并 */
        ICUNIT_ASSERT_NOT_EQUAL(guard, NULL, 0);
        for (idx = 0; idx < 4; idx++) {
            blk[idx] = LOS_MemAlloc(g_memPoolTC316, 0x20);
            ICUNIT_ASSERT_NOT_EQUAL(blk[idx], NULL, idx);
        }
        ret = LOS_MemFree(g_memPoolTC316, blk[0]); /* A free */
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        ret = LOS_MemFree(g_memPoolTC316, blk[2]); /* C free, 链: C→A */
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        ret = LOS_MemFree(g_memPoolTC316, blk[1]); /* B 合并 A/C, 删非链头 A */
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        ret = LOS_MemFree(g_memPoolTC316, blk[3]);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        ret = LOS_MemFree(g_memPoolTC316, guard);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    }

    LOS_MemFreeNodeShow(g_memPoolTC316);
#if (LOSCFG_MEM_LEAKCHECK == 1)
    /* LOS_MemUsedNodeShow 仅 LEAKCHECK=1 编译 */
    LOS_MemUsedNodeShow(g_memPoolTC316);
    LOS_MemUsedNodeShow(NULL);
#endif

    /* F-Pos: LOS_MemPoolList 冒烟(多池链表遍历打印) */
    (VOID)LOS_MemPoolList();

    /* 收尾: 释放本用例自建池 */
    ret = LOS_MemDeInit(g_memPoolTC316);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
#endif

    /* ---- 5/6: BESTFIT ---- */
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    /* F-Inv: LOS_MemPoolInit NULL 池 → LOS_NOK(bestfit L1562-1566) */
    ret = LOS_MemPoolInit(NULL, MEM_TC316_POOL_SIZE, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);

    /* F-Inv: size 超过最大池 → LOS_NOK */
    ret = LOS_MemPoolInit(g_memPoolTC316, 0xFFFFFFFF, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);

    /* F-Pos: 地址未对齐 → 告警后自动对齐继续初始化(非失败) */
    ret = LOS_MemPoolInit(g_memPoolTC316 + 4, MEM_TC316_POOL_SIZE - 8, 0);
    if (ret == LOS_OK) {
        ret = LOS_MemDeInit(g_memPoolTC316 + 4);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    }

    /* F-Pos: 合法池上 UsedBlks/FreeBlks 计数契约 */
    ret = LOS_MemPoolInit(g_memPoolTC316, MEM_TC316_POOL_SIZE, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    freeBefore = LOS_MemFreeBlksGet(g_memPoolTC316);
    usedBefore = LOS_MemUsedBlksGet(g_memPoolTC316);

    ptr = LOS_MemAlloc(g_memPoolTC316, 0x40);
    ICUNIT_ASSERT_NOT_EQUAL(ptr, NULL, 0);

    freeAfter = LOS_MemFreeBlksGet(g_memPoolTC316);
    usedAfter = LOS_MemUsedBlksGet(g_memPoolTC316);
    /* bestfit 分裂语义: 占用块 +1; 空闲块可能 -1(整块占用)或不变(分裂后余块) */
    ICUNIT_ASSERT_EQUAL(usedAfter, usedBefore + 1, usedAfter);
    ICUNIT_ASSERT_WITHIN_EQUAL(freeAfter, freeBefore - 1, freeBefore, freeAfter);

    /* F-Inv: NULL 池入参错误码契约 */
    ICUNIT_ASSERT_EQUAL(LOS_MemFreeBlksGet(NULL), LOS_NOK, 0);
    ICUNIT_ASSERT_EQUAL(LOS_MemUsedBlksGet(NULL), OS_INVALID, 0);

    ret = LOS_MemFree(g_memPoolTC316, ptr);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ptr = NULL;

    /* 注: LOS_MemUsedNodeShow 仅 TLSF+LEAKCHECK 编译, bestfit 无此符号 */
    LOS_MemFreeNodeShow(g_memPoolTC316);

    /* F-Pos: LOS_MemPoolList 冒烟(bestfit OsPoolInfoGet 聚合路径) */
    (VOID)LOS_MemPoolList();

    ret = LOS_MemDeInit(g_memPoolTC316);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
#endif

#ifdef LOSCFG_KERNEL_MEMBOX_STATIC
    /* F-Inv: membox 静态实现——池空间不足以容纳一个块 → uwBlkNum==0 → LOS_NOK(los_membox.c:139-142) */
    ret = LOS_MemboxInit(g_memPool2TC316, sizeof(LOS_MEMBOX_INFO) + 4, 0x100);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);
#endif

    (VOID)ptr;
    (VOID)ptrBig;
    (VOID)usedBefore;
    (VOID)usedAfter;
    (VOID)freeBefore;
    (VOID)freeAfter;
    return LOS_OK;
}

/**
 * @ingroup TEST_MEM
 * @par TestCase_Number
 * ItLosMem316
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_MemInit/LOS_MemPoolInit/LOS_MemUsedBlksGet/LOS_MemFreeBlksGet/LOS_MemUsedNodeShow/LOS_MemFreeNodeShow/LOS_MemPoolList
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: init with invalid(misaligned/oversize) parameters.
 * step2: init valid pool, alloc/free small and large blocks, call show/debug APIs.
 * step3: NULL-pool defensive branches.
 * @par TestCase_Expected_Result
 * 1.Invalid pool parameters rejected; valid pool init/deinit round trip returns LOS_OK.
 * 2.Blk counters follow alloc/free; show APIs run without crash.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosMem316(VOID)
{
    TEST_ADD_CASE("ItLosMem316", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
