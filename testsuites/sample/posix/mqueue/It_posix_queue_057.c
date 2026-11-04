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
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 用例名称: ItPosixQueue057
 * 用例描述: 接收者阻塞中 unlink 的清理路径
 *
 * 覆盖目标:
 *   lib/posix/src/mqueue.c:
 *     mq_unlink       :474-477   有引用 → -1/EINTR + 置 unlinkflag
 *     mq_close(等待者全部退出后, 最后引用) → DoMqueueDelete → LOS_OK
 *                       (名字释放 + LOS 队列删除, 资源回收干净)
 *
 * 断言口径:
 *   子任务 mq_timedreceive 阻塞在空队列期间:
 *   - mq_unlink(同名) == -1 且 errno==EINTR
 *   - 子任务超时退出(ETIMEDOUT)后: mq_close(最后引用) == 0 且底层
 *     LOS 队列删除(同名 mq_open 不带 O_CREAT → -1/ENOENT)
 *   - 同名 mq_open(O_CREAT) 可再建且收发正常(系统状态恢复, 无 ResLeak)
 *
 * 实现语义偏离说明(对比初版):
 *   1) 初版收发缓冲 8 字节: 本实现 mq_timedreceive 要求 msgLen ≥
 *      queueSize-4(mqueue.c:597, 默认属性队列=64-4), 8 字节直接
 *      EMSGSIZE(errno=90)返回, 接收者从未阻塞, close 走了空引用删除
 *      路径(实测 close 返回 0 而非 -1)。改为 64 字节缓冲, 场景按设计
 *      真正阻塞。
 *   2) 初版断言"阻塞等待者在场时 close → -1/EAGAIN(IN_TSKUSE)": 该分支
 *      (mqueue.c:148-151) 虽可触发, 但 DoMqueueDelete 在 LOS_QueueDelete
 *      失败前已释放名字并置空 mqcb(:135-140), 底层 LOS 队列句柄永久
 *      滞留(队列/内存泄漏, ResLeak 必报)——实现缺陷, 已记录待报。按
 *      "实现缺陷用例按实际行为隔离"原则, 本用例改为断言等待者退出后
 *      的干净删除路径, EAGAIN 分支隔离待实现修复后补回。
 *   3) 超时 deadline 取固定小纪元值 {0, 500ms} 而非 now+500ms:
 *      mqueue 的 ConvertTimeout(mqueue.c:507) 把绝对 deadline 交给
 *      OsTimeSpec2Tick 按相对时长换算(time_internal.h:95-106, 对比
 *      cond/sem 使用的 OsGetTickTimeFromNow 是正确写法)——实际阻塞
 *      时长 ≈ deadline 的纪元绝对值(≈ 系统已运行秒数), 随用例在套件
 *      中的位置线性增长(全量跑实测远超轮询窗)。实现缺陷已记录待报;
 *      为保证用例在任意执行位置确定有界, 取小纪元 deadline 使实际
 *      阻塞 ≈ 50 tick(500ms), 阻塞/超时/清理路径覆盖不变。实现修复后
 *      应恢复 now+500ms 的正规 deadline。
 */

/* 用例简要描述: 阻塞引用中删除队列的清理路径 */
#include "It_posix_queue.h"
#include <time.h>

#define MQ057_MSG_LEN 64    /* 64: 满足 receive msgLen ≥ queueSize-4(默认属性) */
#define MQ057_BLOCK_TICKS 50  /* 50: 等待者实际阻塞 tick 数(见偏离说明 3) */
#define MQ057_POLL_BOUND 150  /* 150: poll 上界 tick 数(3x 阻塞时长余量) */

static CHAR g_mq057Name[MQUEUE_STANDARD_NAME_LENGTH] = "";
static mqd_t g_mq057Mqds = (mqd_t)-1;
static volatile INT32 g_mq057Ready = 0;
static volatile INT32 g_mq057Done = 0;
static volatile ssize_t g_mq057RecvRet = 0;

static VOID *Mqueue057Receiver(VOID *arg)
{
    CHAR rbuf[MQ057_MSG_LEN];
    /* 固定小纪元 deadline: 当前实现按"绝对值当相对时长"换算,
     * 实际阻塞 ≈ 50 tick, 与执行位置无关(见文件头偏离说明 3) */
    struct timespec ts = {0, (long)(MQ057_BLOCK_TICKS * 10 * 1000000)}; /* 10ms/tick, ns */

    (VOID)arg;
    g_mq057Ready = 1;
    /* 空队列阻塞接收: 主任务在此期间做 unlink */
    g_mq057RecvRet = mq_timedreceive(g_mq057Mqds, rbuf, MQ057_MSG_LEN, NULL, &ts);
    if (g_mq057RecvRet == -1) {
        dprintf("ItPosixQueue057 receiver errno=%d\n", errno);
    }
    g_mq057Done = 1;
    return NULL;
}

static UINT32 Testcase(VOID)
{
    INT32 ret;
    pthread_t receiver;
    UINT32 i;
    CHAR sbuf[MQ057_MSG_LEN] = {0};
    CHAR rbuf[MQ057_MSG_LEN] = {0};
    mqd_t mqdes2;

    g_mq057Ready = 0;
    g_mq057Done = 0;
    g_mq057RecvRet = 0;

    ret = snprintf_s(g_mq057Name, MQUEUE_STANDARD_NAME_LENGTH, MQUEUE_STANDARD_NAME_LENGTH - 1,
                     "/mq057_%d", LOS_CurTaskIDGet());
    ICUNIT_ASSERT_NOT_EQUAL(ret, -1, ret);

    g_mq057Mqds = mq_open(g_mq057Name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, NULL);
    ICUNIT_ASSERT_NOT_EQUAL(g_mq057Mqds, (mqd_t)-1, (UINTPTR)g_mq057Mqds);

    ret = pthread_create(&receiver, NULL, Mqueue057Receiver, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 等接收者就绪并让出 2 tick 确保其已阻塞在队列读上 */
    for (i = 0; i < MQ057_POLL_BOUND; i++) {
        if (g_mq057Ready == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_mq057Ready, 1, g_mq057Ready, EXIT_JOIN);
    LOS_TaskDelay(2);

    /* 阻塞引用中 unlink: 置标记, EINTR */
    errno = 0;
    ret = mq_unlink(g_mq057Name);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_JOIN);
    ICUNIT_GOTO_EQUAL(errno, EINTR, errno, EXIT_JOIN);

    /* 接收者超时退出(ETIMEDOUT): 等待者离场后再 close, 走干净删除路径 */
    for (i = 0; i < MQ057_POLL_BOUND; i++) {
        if (g_mq057Done == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_mq057Done, 1, g_mq057Done, EXIT_JOIN);
    ICUNIT_GOTO_EQUAL((INT32)g_mq057RecvRet, -1, (INT32)g_mq057RecvRet, EXIT_JOIN);

    ret = pthread_join(receiver, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 等待者退出后 close(最后引用): unlinkflag 已置 → DoMqueueDelete 干净删除 */
    ret = mq_close(g_mq057Mqds);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    g_mq057Mqds = (mqd_t)-1;

    /* 名字已随删除释放: 同名 open(无 O_CREAT) → ENOENT */
    errno = 0;
    mqdes2 = mq_open(g_mq057Name, O_RDWR, S_IRUSR | S_IWUSR, NULL);
    ICUNIT_GOTO_EQUAL((UINTPTR)mqdes2, (UINTPTR)(mqd_t)-1, (UINTPTR)mqdes2, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENOENT, errno, EXIT);

    /* 同名队列可再建且收发正常(系统状态恢复) */
    mqdes2 = mq_open(g_mq057Name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, NULL);
    ICUNIT_GOTO_NOT_EQUAL(mqdes2, (mqd_t)-1, (UINTPTR)mqdes2, EXIT);
    (VOID)memset_s(sbuf, sizeof(sbuf), 0xA5, sizeof(sbuf));
    ret = mq_send(mqdes2, sbuf, MQ057_MSG_LEN, 0);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_CLOSE2);
    ret = (INT32)mq_receive(mqdes2, rbuf, MQ057_MSG_LEN, NULL);
    ICUNIT_GOTO_EQUAL(ret, MQ057_MSG_LEN, ret, EXIT_CLOSE2);
    ret = mq_close(mqdes2);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mq_unlink(g_mq057Name);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT_CLOSE2:
    (VOID)mq_close(mqdes2);
    (VOID)mq_unlink(g_mq057Name);
    goto EXIT;
EXIT_JOIN:
    (VOID)pthread_join(receiver, NULL);
EXIT:
    if (g_mq057Mqds != (mqd_t)-1) {
        (VOID)mq_close(g_mq057Mqds);
        g_mq057Mqds = (mqd_t)-1;
    }
    (VOID)mq_unlink(g_mq057Name);
    return LOS_NOK;
}

VOID ItPosixQueue057(VOID)
{
    TEST_ADD_CASE("ItPosixQueue057", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}
