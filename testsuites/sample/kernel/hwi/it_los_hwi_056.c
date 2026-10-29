/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "it_los_hwi.h"
#include "arch/mmu.h"

#if defined(LOSCFG_ARM_GIC_V3) && defined(LOSCFG_MCS)

/* 用例简要描述: GICD 锁定/解锁往返的页表池零消耗(T6/R1) - 与 GICR 同 L3 表
 * 共生, 往返只清/填 PTE 不动页表池, 三点统计对比四字段不变。 */
static UINT32 Testcase(VOID)
{
    PageTablesStat statOpen;
    PageTablesStat statLocked;
    PageTablesStat statReopen;
    UINT32 ret;

    ret = ArchMmuTableStatistics(&statOpen);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_GicdMmuDisable();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = ArchMmuTableStatistics(&statLocked);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 页表页数不变(同表共生, 无 PageTableAlloc/Free) */
    ICUNIT_GOTO_EQUAL(statLocked.pageUsed, statOpen.pageUsed, statLocked.pageUsed, EXIT);
    /* F-Pos: PTE 条目数精确减少 GICD_MMU_SIZE/4K 个(16) - 框架正确语义 */
    ICUNIT_GOTO_EQUAL((statOpen.pteUsed - statLocked.pteUsed),
                      (UINT32)(GICD_MMU_SIZE / 4096) /* 4K page: target page size */,
                      (statOpen.pteUsed - statLocked.pteUsed), EXIT);

    ret = LOS_GicdMmuEnable();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = ArchMmuTableStatistics(&statReopen);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 解锁恢复后页数不变且 PTE 条目数复原(重填既有 L3 表, 零分配) */
    ICUNIT_GOTO_EQUAL(statReopen.pageUsed, statOpen.pageUsed, statReopen.pageUsed, EXIT);
    ICUNIT_GOTO_EQUAL(statReopen.pteUsed, statOpen.pteUsed, statReopen.pteUsed, EXIT);

    return LOS_OK;
EXIT:
    (VOID)LOS_GicdMmuEnable();
    return LOS_OK;
}

/**
 * @ingroup TEST_HWI
 * @par TestCase_Number
 * ItLosHwi056
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Page-table pool stays constant across lock/unlock round-trips
 * @par TestCase_Pretreatment_Condition
 * RK3568 AMP slave.
 * @par TestCase_Test_Steps
 * step1: sample ArchMmuTableStatistics with the window open.
 * step2: lock and sample again.
 * step3: unlock and sample a third time.
 * @par TestCase_Expected_Result
 * 1.pageUsed/pteUsed identical in all three samples: the GICD window
 *   shares its L3 table with the never-unmapped GICR region, so the
 *   round-trip neither allocates nor frees pool entries (R1 premise).
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 */
VOID ItLosHwi056(VOID)
{
#if defined(LOSCFG_ARM_GIC_V3) && defined(LOSCFG_MCS)
    /* 页表池往返零消耗 */
    TEST_ADD_CASE("ItLosHwi056", Testcase, TEST_LOS, TEST_HWI, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
#endif /* LOSCFG_ARM_GIC_V3 && LOSCFG_MCS */
