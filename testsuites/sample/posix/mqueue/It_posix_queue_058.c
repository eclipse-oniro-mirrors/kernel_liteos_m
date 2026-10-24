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
 *            拦截未触达泄漏分支(errno=36), 名长改为 254(含斜杠总长 255)。 */
#include "It_posix_queue.h"
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>
#include "los_memory.h"

#define MQ058_NAME_LEN     254 /* 含斜杠总长 255 = PATH_MAX-1, 通过 MqNameCheck 后名字串分配 256B */
#define MQ058_DRAIN_BIG    8192
#define MQ058_DRAIN_MID    256
#define MQ058_DRAIN_SMALL  64
#define MQ058_FREE_CNT     2
#define MQ058_MAX_BLOCKS   4096

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

    mqName[0] = '/';
    for (i = 1; i <= MQ058_NAME_LEN; i++) {
        mqName[i] = 'n';
    }
    mqName[MQ058_NAME_LEN + 1] = '\0';

    queueBefore = QueueUsedCountGet();

    /* 多粒度耗尽堆: 8KB -> 256B -> 64B, 全部记账待还 */
    Mq058DrainPass(MQ058_DRAIN_BIG, drainBlocks, &drainUsed, MQ058_MAX_BLOCKS);
    Mq058DrainPass(MQ058_DRAIN_MID, drainBlocks, &drainUsed, MQ058_MAX_BLOCKS);
    Mq058DrainPass(MQ058_DRAIN_SMALL, drainBlocks, &drainUsed, MQ058_MAX_BLOCKS);

    /* 释放 2 个最小块: 制造仅够内核队列缓冲(~16B)、不够 256B 名字串的空洞 */
    for (i = 0; i < MQ058_FREE_CNT; i++) {
        (VOID)LOS_MemFree(m_aucSysMem0, drainBlocks[drainUsed - 1 - i]);
        drainBlocks[drainUsed - 1 - i] = NULL;
    }

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
        /* 名字分配失败时 mq_open 必然失败; 若成功说明环境异常(堆未按预期耗尽),
         * 关闭句柄保持用例自清理, 队列计数断言仍交由下方统一判定 */
        (VOID)mq_close(mqd);
        (VOID)mq_unlink(mqName);
    }

    /* 修复后契约: 失败的 mq_open 不得残留已创建的内核队列 */
    ICUNIT_ASSERT_EQUAL(queueAfter, queueBefore, queueAfter);

    return LOS_OK;
}

VOID ItPosixQueue058(VOID)
{
    TEST_ADD_CASE("ItPosixQueue058", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
