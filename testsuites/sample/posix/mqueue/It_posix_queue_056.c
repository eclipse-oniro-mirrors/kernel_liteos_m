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
 * 用例名称: ItPosixQueue056
 * 用例描述: 调度锁下定时收发被拒返回
 *
 * 覆盖目标:
 *   lib/posix/src/mqueue.c:
 *     MapMqErrno :71-73  default 分支(QUEUE_PEND_IN_LOCK 未映射) → errno=EINVAL
 *     mq_timedsend/mq_timedreceive 经 LOS_QueueWriteCopy/LOS_QueueReadCopy 在
 *     调度锁内 pend 失败 → 走 default 映射返回 EINVAL
 *
 * 断言口径:
 *   maxmsg=1 消息队列, 发送 1 条占满后:
 *   - LOS_TaskLock 内 mq_timedsend(满队列, 有效未来超时) == -1 且 errno==EINVAL
 *   - 接收清空后, LOS_TaskLock 内 mq_timedreceive(空队列, 有效未来超时) == -1
 *     且 errno==EINVAL
 *   - 解锁后收发恢复正常
 */

/* 用例简要描述: 调度锁下定时收发被拒返回 */
#include "It_posix_queue.h"
#include <time.h>

#define MQ056_MSG_LEN 8 /* 8: message size */

static UINT32 Testcase(VOID)
{
    CHAR mqname[MQUEUE_STANDARD_NAME_LENGTH] = "";
    mqd_t mqdes;
    struct mq_attr attr = {0};
    struct timespec ts = {0};
    CHAR msg[MQ056_MSG_LEN] = "mq05600";
    CHAR rbuf[MQ056_MSG_LEN];
    struct timespec now = {0};
    INT32 ret;

    ret = snprintf_s(mqname, MQUEUE_STANDARD_NAME_LENGTH, MQUEUE_STANDARD_NAME_LENGTH - 1,
                     "/mq056_%d", LOS_CurTaskIDGet());
    ICUNIT_ASSERT_NOT_EQUAL(ret, -1, ret);

    attr.mq_maxmsg = 1;
    attr.mq_msgsize = MQ056_MSG_LEN;
    mqdes = mq_open(mqname, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
    ICUNIT_GOTO_NOT_EQUAL(mqdes, (mqd_t)-1, (UINTPTR)mqdes, EXIT1);

    /* 有效未来超时(+1s): 保证进入 pend 路径而非 NO_WAIT */
    (VOID)clock_gettime(CLOCK_REALTIME, &now);
    ts.tv_sec = now.tv_sec + 1;
    ts.tv_nsec = now.tv_nsec;

    /* 发送 1 条占满(maxmsg=1) */
    ret = mq_send(mqdes, msg, MQ056_MSG_LEN, 0);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 调度锁内满发: QUEUE_PEND_IN_LOCK → MapMqErrno default → EINVAL */
    LOS_TaskLock();
    errno = 0;
    ret = mq_timedsend(mqdes, msg, MQ056_MSG_LEN, 0, &ts);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_UNLOCK);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT_UNLOCK);
    LOS_TaskUnlock();

    /* 正常接收清空 */
    ret = mq_receive(mqdes, rbuf, MQ056_MSG_LEN, NULL);
    ICUNIT_GOTO_EQUAL(ret, MQ056_MSG_LEN, ret, EXIT);

    /* 调度锁内空收: 同走 default 映射 EINVAL */
    LOS_TaskLock();
    errno = 0;
    ret = mq_timedreceive(mqdes, rbuf, MQ056_MSG_LEN, NULL, &ts);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_UNLOCK);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT_UNLOCK);
    LOS_TaskUnlock();

    /* 解锁后恢复正常收发 */
    ret = mq_send(mqdes, msg, MQ056_MSG_LEN, 0);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mq_receive(mqdes, rbuf, MQ056_MSG_LEN, NULL);
    ICUNIT_GOTO_EQUAL(ret, MQ056_MSG_LEN, ret, EXIT);

    ret = mq_close(mqdes);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT2);
    ret = mq_unlink(mqname);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT1);
    return LOS_OK;
EXIT_UNLOCK:
    LOS_TaskUnlock();
EXIT:
    (VOID)mq_close(mqdes);
    goto EXIT1;
EXIT2:
    (VOID)mq_unlink(mqname);
EXIT1:
    return LOS_NOK;
}

VOID ItPosixQueue056(VOID)
{
    TEST_ADD_CASE("ItPosixQueue056", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
