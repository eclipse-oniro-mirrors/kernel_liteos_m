/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary, with or without modification,
 * are permitted under the conditions of the LOSCFG-style open source license.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 */

/* 用例简要描述: 【BUG_24 缺陷暴露用例(红灯)】OsSlabStatisticsGet 孤儿零调用——slab 内部泄漏不可观测
 *
 * 【现象】    LOSCFG_KERNEL_MEM_SLAB_EXTENTION 启用板(ws63/hi3322)上, <=128B
 *            小对象经 slab 大块内部切分; 分配 4 个 36B 且不释放(制造真实泄漏)
 *            后, 公共统计 API LOS_MemTotalUsedGet 读数不变(delta=0)——slab 内部
 *            泄漏不可观测。OsSlabStatisticsGet 能正确报 usedSize +272(函数工作),
 *            但该 API 全仓零调用(孤儿), 无公共途径暴露 slab 内部用量。
 * 【根因】    OsSlabStatisticsGet(los_slabmem.c:505) extern 声明(los_slab_pri.h:
 *            107)并完整实现(5 字段), 但全仓 grep 零 consumer。LOS_MemTotalUsedGet
 *            不合并 slab 统计=设计如此(slab 大块整体已计入, 合并会重复计算);
 *            但 OsSlabStatisticsGet 既然 extern 声明并完整实现, 意图就是供模块外
 *            消费; 零调用=集成遗漏或死代码 -> slab 内部用量无公共 API 可观测。
 * 【修复方向】提供公共 API(如 LOS_SlabStatGet)暴露 slab usedSize, 或在
 *            LOS_MemInfoGet 附加 slab 字段; 修复后公共途径能反映 slab 内部
 *            用量变化(delta > 0)。
 * 【恢复方法】修复后把 bug_isolate.h 中 KERNEL_BUG_24_SLAB_STAT_BLIND 置 1,
 *            聚合注册点(it_los_mem.c)恢复调用, 用例恢复执行作为回归看护常跑。
 * 【关联】    BUG_24 / resleak 批次问题记录 20261023_213008。
 *            板级: 仅 LOSCFG_KERNEL_MEM_SLAB_EXTENTION 启用板(如 ws63/hi3322)注册;
 *            qemu 未启用 slab, 用例不注册。 */
#include "It_los_mem.h"
#if defined(LOSCFG_KERNEL_MEM_SLAB_EXTENTION)
#include "los_slab_pri.h"
#endif

#define MEM_SLAB_PROBE_COUNT   4
#define MEM_SLAB_PROBE_SIZE    36

static UINT32 Testcase(VOID)
{
    VOID *ptrs[MEM_SLAB_PROBE_COUNT];
    UINT32 i;
    UINT32 usedBefore;
    UINT32 usedAfter;
    LOS_MEM_POOL_STATUS infoBefore = {0};
    LOS_MEM_POOL_STATUS infoAfter = {0};
#if defined(LOSCFG_KERNEL_MEM_SLAB_EXTENTION)
    UINT32 slabUsedBefore = 0;
    UINT32 slabUsedAfter = 0;
    LosSlabStatus slabBefore = {0};
    LosSlabStatus slabAfter = {0};
#endif

    for (i = 0; i < MEM_SLAB_PROBE_COUNT; i++) {
        ptrs[i] = NULL;
    }

    usedBefore = LOS_MemTotalUsedGet(m_aucSysMem0);
    (VOID)memset_s(&infoBefore, sizeof(infoBefore), 0, sizeof(infoBefore));
    (VOID)LOS_MemInfoGet(m_aucSysMem0, &infoBefore);
#if defined(LOSCFG_KERNEL_MEM_SLAB_EXTENTION)
    (VOID)OsSlabStatisticsGet(m_aucSysMem0, &slabBefore);
    slabUsedBefore = slabBefore.usedSize;
#endif

    /* 4 个 36B 小对象: <=128B 且不超 slab 桶初始容量, 全走 slab 内部切分
     * (不增节点链、不触发 slab 扩张); 不释放——制造 slab 内部真实泄漏 */
    for (i = 0; i < MEM_SLAB_PROBE_COUNT; i++) {
        ptrs[i] = malloc(MEM_SLAB_PROBE_SIZE);
        if (ptrs[i] == NULL) {
            break;
        }
    }
    ICUNIT_ASSERT_NOT_EQUAL(i, 0, i);

    usedAfter = LOS_MemTotalUsedGet(m_aucSysMem0);
    (VOID)memset_s(&infoAfter, sizeof(infoAfter), 0, sizeof(infoAfter));
    (VOID)LOS_MemInfoGet(m_aucSysMem0, &infoAfter);
#if defined(LOSCFG_KERNEL_MEM_SLAB_EXTENTION)
    (VOID)OsSlabStatisticsGet(m_aucSysMem0, &slabAfter);
    slabUsedAfter = slabAfter.usedSize;
#endif

    dprintf("[Mem333] TotalUsedGet before=%u after=%u delta=%d (公共 API 看不到 slab 内泄漏)\n",
            usedBefore, usedAfter, (INT32)(usedAfter - usedBefore));
    dprintf("[Mem333] InfoGet usedBlk before=%u after=%u\n",
            infoBefore.usedNodeNum, infoAfter.usedNodeNum);
#if defined(LOSCFG_KERNEL_MEM_SLAB_EXTENTION)
    dprintf("[Mem333] SlabStat usedSize before=%u after=%u delta=%d (孤儿 API 能报数但无公共调用方, slab 泄漏不可观测)\n",
            slabUsedBefore, slabUsedAfter, (INT32)(slabUsedAfter - slabUsedBefore));
#endif

    /* 修复后契约: slab 内部泄漏后, 公共统计 API 应能检测到用量变化(delta > 0)。
     * 修复前: LOS_MemTotalUsedGet delta=0(slab 大块已计入, 内部分配不变) ->
     *         OsSlabStatisticsGet 孤儿无公共调用方 -> slab 泄漏不可观测 -> RED。
     * 修复后: 提供公共 API 暴露 slab usedSize(或 LOS_MemInfoGet 附加 slab 字段)
     *         -> 该公共途径反映 slab 内部用量变化 -> delta > 0 -> GREEN。 */
    ICUNIT_ASSERT_NOT_EQUAL(usedAfter, usedBefore, usedAfter);

    /* 不清理: 4 个 36B 小对象保留不释放, 制造 slab 内部真实泄漏
     * (slab 额度被永久占用, 但公共 API 检测不到) */
    return LOS_OK;
}

VOID ItLosMem333(VOID)
{
    TEST_ADD_CASE("ItLosMem333", Testcase, TEST_LOS, TEST_MEM, TEST_LEVEL0, TEST_FUNCTION);
}
