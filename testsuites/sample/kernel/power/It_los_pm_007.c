/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the conditions of the LOSCFG-style open source license.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 */

/* 用例简要描述: 【BUG_21 缺陷暴露用例】PM 唤醒锁全局计数打满后新锁节点插入链表不回滚泄漏
 *
 * 【现象】    LOS_PmLockRequest 同名循环持有至全局计数 pm->lock 达上限(0xFFFF)后,
 *            以新名字再请求: OsPmLockRequest 先分配 OsPmLockCB 节点并插入
 *            pm->lockList, 之后才发现 pm->lock 已满, 返回 LOS_ERRNO_PM_NOT_LOCK
 *            但节点不摘除不释放 -> 调用方收到失败不会再去 Release, 节点永久
 *            滞留链表(每次重复触发泄漏一个节点)。
 *            附带缺陷: 若按该失败名字调 LOS_PmLockRelease, 会在未持锁的情况下
 *            pm->lock--, 偷减其他持有者的全局计数。
 * 【根因】    kernel/extended/power/los_pm.c:594-617 OsPmLockRequest 资源获取
 *            顺序颠倒: 节点分配+插入链表(:603)先于全局容量检查
 *            (:614 if (pm->lock < OS_PM_LOCK_MAX)), 失败路径无回滚。
 * 【修复方向】先检查 pm->lock < OS_PM_LOCK_MAX 再分配插入; 或失败时将节点从
 *            lockList 摘除并 LOS_MemFree 后再返回错误。
 * 【恢复方法】修复后把 bug_isolate.h 中 KERNEL_BUG_21_PM_LOCKCNT_FULL_LEAK 置 1,
 *            聚合注册点(It_los_pm.c)恢复调用, 用例恢复执行作为回归看护常跑。
 * 【关联】    BUG_21 / resleak 批次问题记录 20261023_213005。 */
#include "osTest.h"
#include "It_los_pm.h"
#include "los_pm.h"
#include "los_memory.h"

#define PM007_LOCK_MAX      0xFFFFU /* 与 los_pm.c OS_PM_LOCK_MAX 保持一致 */
#define PM007_NAME_A        "pmLeakA"
#define PM007_NAME_B        "pmLeakB"

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 held = 0;
    UINT32 i;
    UINT32 reqRetB;
    UINT32 memUsedBefore;
    UINT32 memUsedAfter;

    memUsedBefore = LOS_MemTotalUsedGet(m_aucSysMem0);

    /* 同名请求填满全局计数: 直到返回失败(含 pm->lock 已被系统占用的情况) */
    for (i = 0; i <= PM007_LOCK_MAX; i++) {
        ret = LOS_PmLockRequest(PM007_NAME_A);
        if (ret != LOS_OK) {
            break;
        }
        held++;
    }
    ICUNIT_ASSERT_NOT_EQUAL(held, 0, held);
    dprintf("[Pm007] lockA held=%u, fill-stop ret=0x%x\n", held, ret);

    /* 全局计数已满, 新名字请求: 应失败且不留任何节点 */
    reqRetB = LOS_PmLockRequest(PM007_NAME_B);
    dprintf("[Pm007] lockB req ret=0x%x (expect error, no residue)\n", reqRetB);

    /* 释放全部同名锁, 恢复全局计数与节点 A */
    for (i = 0; i < held; i++) {
        ret = LOS_PmLockRelease(PM007_NAME_A);
        if (ret != LOS_OK) {
            break;
        }
    }

    memUsedAfter = LOS_MemTotalUsedGet(m_aucSysMem0);
    dprintf("[Pm007] memUsed before=%u after=%u delta=%d (expect 0)\n",
            memUsedBefore, memUsedAfter, (INT32)(memUsedAfter - memUsedBefore));

    /* 修复后契约: 失败的锁请求不得残留节点, 全序列后内存用量必须复原 */
    ICUNIT_ASSERT_EQUAL(memUsedAfter, memUsedBefore, memUsedAfter);

    return LOS_OK;
}

VOID ItLosPm007(VOID)
{
    TEST_ADD_CASE("ItLosPm007", Testcase, TEST_LOS, TEST_PM, TEST_LEVEL0, TEST_FUNCTION);
}
