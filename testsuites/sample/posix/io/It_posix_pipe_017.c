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

/* 用例简要描述: 【BUG_17 缺陷暴露用例】poll() 监听多个无事件 fd 时 PollWaitNode 堆节点泄漏
 *
 * 【现象】    poll() 同时监听 >=2 个无就绪事件的管道读端(timeout != 0)时, 每次调用
 *            永久泄漏 (nfds-1) 个 struct PollWaitNode 堆节点; 泄漏节点仍挂在各管道
 *            dev->wq 等待队列上, 管道关闭/设备注销也不回收(PipeDevUnregister 不
 *            遍历 wq 释放节点), 循环调用可无限累积堆内存。
 * 【根因】    lib/posix/src/poll.c:84 AddPollWaitQueue 中 table->node 为单指针,
 *            QueryFds(poll.c:108-127) 对每个未就绪 fd 调 PipePoll→PollWait→
 *            AddPollWaitQueue 分配新节点并直接覆盖 table->node 旧指针;
 *            DestroyPollWait(poll.c:52-71) 只释放最后一个节点, 前 n-1 个节点
 *            既无人释放也未从各管道 wq 摘除。
 * 【修复方向】PollTable.node 改为节点链表头(或 AddPollWaitQueue 发现 table 已挂
 *            节点时先摘除旧节点), DestroyPollWait 遍历释放全部节点并从 wq 摘除。
 * 【关联】    BUG_17 / resleak 批次问题记录 20261023_213001。 */
#include "It_posix_io.h"
#include <poll.h>
#include "los_memory.h"

#define PIPE017_ROUNDS 2

static UINT32 Testcase(VOID)
{
    int fa[2]; /* 2, pipe fd pair */
    int fb[2]; /* 2, pipe fd pair */
    int ret;
    UINT32 round;
    UINT32 memUsedBefore;
    UINT32 memUsedAfter;
    struct pollfd pfds[2]; /* 2, two not-ready fds in one poll call */
    int warmup[2]; /* 2, warmup pipe fd pair */

    /* 预热: 吸收 pipe 子系统/设备链表锁的单例懒初始化, 避免污染本例内存基线 */
    ret = pipe(warmup);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    close(warmup[0]);
    close(warmup[1]);

    ret = pipe(fa);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = pipe(fb);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    memUsedBefore = LOS_MemTotalUsedGet(m_aucSysMem0);

    /* 每轮: 2 个无数据管道读端 + 非零超时 -> 首 QueryFds 对两个 fd 各分配一个
     * PollWaitNode(表指针覆盖), DestroyPollWait 只释放最后一个 -> 每轮泄漏 1 个 */
    for (round = 0; round < PIPE017_ROUNDS; round++) {
        (VOID)memset_s(pfds, sizeof(pfds), 0, sizeof(pfds));
        pfds[0].fd = fa[0];
        pfds[0].events = POLLIN;
        pfds[1].fd = fb[0];
        pfds[1].events = POLLIN;
        ret = poll(pfds, 2, 50); /* 2: nfds, 50: timeout ms */
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }

    memUsedAfter = LOS_MemTotalUsedGet(m_aucSysMem0);
    dprintf("[Pipe017] memUsed before=%u after=%u delta=%d (expect 0)\n",
            memUsedBefore, memUsedAfter, (INT32)(memUsedAfter - memUsedBefore));

    /* 修复后契约: poll 返回后所有等待节点均已释放, 内存用量必须复原 */
    ICUNIT_GOTO_EQUAL(memUsedAfter, memUsedBefore, memUsedAfter, EXIT);

EXIT:
    close(fa[0]);
    close(fa[1]);
    close(fb[0]);
    close(fb[1]);
    return LOS_OK;
}

VOID ItPosixPipe017(VOID)
{
    TEST_ADD_CASE("ItPosixPipe017", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
