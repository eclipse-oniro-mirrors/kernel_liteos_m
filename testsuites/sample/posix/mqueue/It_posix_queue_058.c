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

/* 用例简要描述: 【BUG_22 缺陷暴露用例】mq_open 名字保存失败后已创建内核队列不删除而泄漏
 *
 * 【现象】    堆内存紧张(仅剩小块碎片)时 mq_open(O_CREAT) 以 attr{mq_maxmsg=1,
 *            mq_msgsize=1} 创建: 内核队列(LOS_QueueCreate: 静态 CB 槽位 + 消息
 *            缓冲堆内存)创建成功后, SaveMqueueName 为长名字(总长 255=PATH_MAX-1,
 *            通过 MqNameCheck)分配 nameLen+1=256B 内存失败 -> mq_open 返回
 *            -1/ENOMEM, 但刚创建的内核队列既未 LOS_QueueDelete 也无任何 POSIX
 *            句柄可回收 -> 队列槽位+缓冲永久泄漏; 反复触发可耗尽
 *            LOSCFG_BASE_IPC_QUEUE_LIMIT, 之后所有 mq_open(O_CREAT)/
 *            LOS_QueueCreate 返回队列池满。
 * 【根因】    lib/posix/src/mqueue.c:199-201 DoMqueueCreate 在 SaveMqueueName
 *            失败分支 goto ERROUT, 而 ERROUT(:224-230) 仅释放 mq_name, 不调
 *            LOS_QueueDelete; 对照同函数 mq_personal 分配失败路径(:208-215)
 *            有完整的队列删除, 证明该分支系遗漏(GetMqueueCBByID 失败分支
 *            :203-206 同样漏删)。
 * 【修复方向】SaveMqueueName/GetMqueueCBByID 失败分支在 ERROUT 前调用
 *            LOS_QueueDelete(mqueueID) 归还队列资源(注意失败时 mqueueCB 可能
 *            已登记 g_queueTable, 需先清表再删队列, 与 mq_personal 分支对齐)。
 * 【恢复方法】修复后把 bug_isolate.h 中 POSIX_BUG_22_MQOPEN_NAMEFAIL_LEAK 置 1,
 *            聚合注册点(It_posix_queue.c)恢复调用, 用例恢复执行作为回归看护常跑。
 * 【关联】    BUG_22 / resleak 批次问题记录 20261023_213006。
 * 修正记录:  首轮取证名字总长 256 > PATH_MAX-1 被 MqNameCheck 以 ENAMETOOLONG
 *            拦截未触达泄漏分支(errno=36), 名长改为 254(含斜杠总长 255)。
 *            二轮造洞依赖"64B 排水轮恰好分到 >=2 块"的偶发前提: 20261027 短跑轮
 *            (qemu_test_20261027_232406) 256B 轮已把堆耗至 max free node 0x28,
 *            64B 轮 0 块 -> 无条件释放栈顶两块误放 2x0x110 大洞, 名字串分配成功,
 *            mq_open 整体成功, 队列计数 +1 被误判为泄漏(Errline:118 RetCode:0x2)。
 *            三轮改为确定性造洞: 排水后释放数组末块(必属实际分到块的最小粒度轮),
 *            再以 LOS_MemInfoGet 实测驱动回填 0xC0 块直至最大空闲节点 < 0x110,
 *            洞径恒落于 [0x40, 0x110), 与前置用例碎片状态解耦, 必然停在
 *            SaveMqueueName 的 ENOMEM 分支。 */
#include "It_posix_queue.h"
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>
#include "los_memory.h"

#define MQ058_NAME_LEN     254 /* 含斜杠总长 255 = PATH_MAX-1, 通过 MqNameCheck 后名字串分配 256B */
#define MQ058_DRAIN_BIG    8192
#define MQ058_DRAIN_MID    256
#define MQ058_DRAIN_SMALL  64
#define MQ058_MAX_BLOCKS   4096
#define MQ058_FILL_SIZE    192 /* 回填块 0xC0: 节点 0xD0 > 0x50, 只可能取自造出的洞 */
#define MQ058_NAME_NODE    0x110 /* 256B 名字串所需节点 = 256 + 0x10 节点头 */

static VOID Mq058DrainPass(UINT32 size, VOID **blocks, UINT32 *used, UINT32 cap)
{
    UINT32 i;

    for (i = 0; i < cap; i++) {
        VOID *p = LOS_MemAlloc(m_aucSysMem0, size);
        if (p == NULL) {
            break;
        }
        blocks[(*used)++] = p;
    }
}

static UINT32 Testcase(VOID)
{
    mqd_t mqd;
    struct mq_attr attr;
    UINT32 i;
    UINT32 queueBefore;
    UINT32 queueAfter;
    UINT32 drainUsed = 0;
    CHAR mqName[MQ058_NAME_LEN + 2]; /* 2: 斜杠与结束符 */
    static VOID *drainBlocks[MQ058_MAX_BLOCKS];
    LOS_MEM_POOL_STATUS memSt = {0};

    mqName[0] = '/';
    for (i = 1; i <= MQ058_NAME_LEN; i++) {
        mqName[i] = 'n';
    }
    mqName[MQ058_NAME_LEN + 1] = '\0';

    queueBefore = QueueUsedCountGet();

    /* 多粒度耗尽堆: 8KB -> 256B -> 64B, 全部记账待还。
     * 64B 轮以 ENOMEM 收尾 => 此刻全池空闲节点均 < 0x50 */
    Mq058DrainPass(MQ058_DRAIN_BIG, drainBlocks, &drainUsed, MQ058_MAX_BLOCKS);
    Mq058DrainPass(MQ058_DRAIN_MID, drainBlocks, &drainUsed, MQ058_MAX_BLOCKS);
    Mq058DrainPass(MQ058_DRAIN_SMALL, drainBlocks, &drainUsed, MQ058_MAX_BLOCKS);

    /* 确定性造洞(与前置用例遗留碎片状态解耦):
     * 1) 释放数组末块 —— 三轮由大到小依次入账, 末块必属实际分到块的最小粒度轮
     *    (节点 >= 0x50), 邻居或在用或 < 0x50, 释放后全池仅此一个 >= 0x50 的洞;
     * 2) LOS_MemInfoGet 实测驱动回填: 只要最大空闲节点 >= 0x110(名字串节点)就
     *    再填一块 0xC0(节点 0xD0, 大于排水块只能取自该洞), 退出时洞径恒在
     *    [0x40, 0x110) —— 容得下 LOS_QueueCreate 消息缓冲(len*maxMsgSize=5,
     *    节点 0x18) 而容不下 256B 名字串, mq_open 必然停在 SaveMqueueName 的
     *    ENOMEM 分支(BUG_22 暴露路径), 不再依赖 64B 轮恰好分到块的前提。 */
    if (drainUsed > 0) {
        (VOID)LOS_MemFree(m_aucSysMem0, drainBlocks[drainUsed - 1]);
        drainBlocks[drainUsed - 1] = NULL;
    }
    while (drainUsed < MQ058_MAX_BLOCKS) {
        if ((LOS_MemInfoGet(m_aucSysMem0, &memSt) != LOS_OK) ||
            (memSt.maxFreeNodeSize < MQ058_NAME_NODE)) {
            break;
        }
        VOID *filler = LOS_MemAlloc(m_aucSysMem0, MQ058_FILL_SIZE);
        if (filler == NULL) {
            break;
        }
        drainBlocks[drainUsed++] = filler;
    }
    (VOID)LOS_MemInfoGet(m_aucSysMem0, &memSt);
    dprintf("[Queue058] hole ready: maxFreeNode=0x%x (expect < 0x110)\n", memSt.maxFreeNodeSize);

    attr.mq_maxmsg = 1;
    attr.mq_msgsize = 1;
    attr.mq_flags = 0;
    mqd = mq_open(mqName, O_CREAT | O_RDWR, 0666, &attr); /* 0666: mode */
    dprintf("[Queue058] mq_open failed=%d errno=%d (expect failed=1/ENOMEM=12, no queue residue)\n",
            (mqd == (mqd_t)-1) ? 1 : 0, errno);

    queueAfter = QueueUsedCountGet();
    dprintf("[Queue058] queueUsed before=%u after=%u (expect equal)\n", queueBefore, queueAfter);

    /* 无论成败先归还全部排水块恢复堆, 保持环境无污染 */
    for (i = 0; i < drainUsed; i++) {
        if (drainBlocks[i] != NULL) {
            (VOID)LOS_MemFree(m_aucSysMem0, drainBlocks[i]);
            drainBlocks[i] = NULL;
        }
    }

    if (mqd != (mqd_t)-1) {
        /* 名字分配失败时 mq_open 必然失败; 若成功说明造洞异常(理论上不可达,
         * 洞径已由 LOS_MemInfoGet 封顶在名字串节点以内), 关闭句柄保持用例
         * 自清理, 队列计数断言仍交由下方统一判定 */
        (VOID)mq_close(mqd);
        (VOID)mq_unlink(mqName);
    }

    /* 修复后契约: 失败的 mq_open 不得残留已创建的内核队列 */
    ICUNIT_ASSERT_EQUAL(queueAfter, queueBefore, queueAfter);

    return LOS_OK;
}

VOID ItPosixQueue058(VOID)
{
    TEST_ADD_CASE("ItPosixQueue058", Testcase, TEST_LIB, TEST_FN_COMPAT_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
