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

/* 用例简要描述: 【BUG_18 缺陷暴露用例】mux 池耗尽时 pthread_cond_init 假成功且 destroy 泄漏堆块
 *
 * 【现象】    内核 mux 池耗尽(LOS_MuxCreate 返回 ALL_BUSY)时:
 *            1) pthread_cond_init 对内部锁 pthread_mutex_init 失败毫无感知,
 *               仍返回 0(假成功), cond->mutex 指向未初始化(magic 为垃圾值)堆块;
 *            2) 后续 pthread_cond_destroy 中 LOS_EventDestroy 成功后,
 *               pthread_mutex_destroy 因 magic 校验失败返回 EINVAL,
 *               提前 return 跳过 free(cond->mutex) -> 每对 init/destroy 泄漏
 *               一个 sizeof(pthread_mutex_t) 堆块, 无任何对外接口可回收。
 * 【根因】    lib/posix/src/pthread_cond.c:159 init 忽略 pthread_mutex_init 返回值;
 *            pthread_cond.c:183-186 destroy 在 mutex_destroy 失败时 return EINVAL,
 *            跳过 :187 的 free(cond->mutex)。
 * 【修复方向】init: 内部锁初始化失败时 free(cond->mutex) 并返回错误码(如 EAGAIN),
 *            不应假成功; destroy: 无论 mutex_destroy 结果如何都要释放 cond->mutex
 *            (半初始化对象更应回收), 或 init 失败时置 cond->mutex=NULL 使 destroy 走空路径。
 * 【恢复方法】修复后把 bug_isolate.h 中 POSIX_BUG_18_PTHREAD_COND_MUTEX_LEAK 置 1,
 *            聚合注册点(It_posix_pthread.c)恢复调用, 用例恢复执行作为回归看护常跑。
 * 【关联】    BUG_18 / resleak 批次问题记录 20261023_213002。 */
#include "It_posix_pthread.h"
#include "los_mux.h"
#include "los_memory.h"

static UINT32 Testcase(VOID)
{
    INT32 ret;
    INT32 dret;
    UINT32 i;
    UINT32 created = 0;
    UINT32 memUsedBefore;
    UINT32 memUsedAfter;
    pthread_cond_t cond;
    UINT32 muxHandles[LOSCFG_BASE_IPC_MUX_LIMIT];

    /* 耗尽内核 mux 池: 循环创建直到 ALL_BUSY(系统已占用少量槽位, 实际可建数 < LIMIT) */
    for (i = 0; i < LOSCFG_BASE_IPC_MUX_LIMIT; i++) {
        ret = (INT32)LOS_MuxCreate(&muxHandles[i]);
        if (ret != LOS_OK) {
            break;
        }
        created++;
    }
    ICUNIT_ASSERT_NOT_EQUAL(created, 0, created);

    memUsedBefore = LOS_MemTotalUsedGet(m_aucSysMem0);

    /* 池探针(ws63 取证): malloc 归属池与 m_aucSysMem0 区间比对 */
    {
        VOID *probeP = malloc(64); /* 64: 探针块 */
        dprintf("[Pthread043] poolProbe m0=%p malloc64=%p insideM0=%d\n",
                (VOID *)m_aucSysMem0, probeP,
                (((UINTPTR)probeP > (UINTPTR)m_aucSysMem0) &&
                 ((UINTPTR)probeP < ((UINTPTR)m_aucSysMem0 + 0x80000))) ? 1 : 0); /* 0x80000: 区间上界 */
        free(probeP);
    }

    (VOID)memset_s(&cond, sizeof(cond), 0, sizeof(cond));
    ret = pthread_cond_init(&cond, NULL);
    dprintf("[Pthread043] cond_init ret=%d (mux pool exhausted)\n", ret);
    if (ret == 0) {
        dret = pthread_cond_destroy(&cond);
        dprintf("[Pthread043] cond_destroy ret=%d\n", dret);
    }

    memUsedAfter = LOS_MemTotalUsedGet(m_aucSysMem0);
    dprintf("[Pthread043] leaked cond.mutex=%p insideM0=%d\n", cond.mutex,
            (((UINTPTR)cond.mutex > (UINTPTR)m_aucSysMem0) &&
             ((UINTPTR)cond.mutex < ((UINTPTR)m_aucSysMem0 + 0x80000))) ? 1 : 0); /* 0x80000: 区间上界 */
    dprintf("[Pthread043] memUsed before=%u after=%u delta=%d (expect 0)\n",
            memUsedBefore, memUsedAfter, (INT32)(memUsedAfter - memUsedBefore));

    /* 修复后契约: 无论 init 成败, init+destroy 序列后不得残留堆块 */
    ICUNIT_GOTO_EQUAL(memUsedAfter, memUsedBefore, memUsedAfter, EXIT);

EXIT:
    for (i = 0; i < created; i++) {
        (VOID)LOS_MuxDelete(muxHandles[i]);
    }
    return LOS_OK;
}

VOID ItPosixPthread043(VOID)
{
    TEST_ADD_CASE("ItPosixPthread043", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
