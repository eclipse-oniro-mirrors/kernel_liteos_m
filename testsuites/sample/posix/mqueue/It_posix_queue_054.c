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
 * 用例名称: ItPosixQueue054
 * 用例描述: unlink 置 flag 后同名队列打开被拒
 *
 * 覆盖目标:
 *   lib/posix/src/mqueue.c:
 *     mq_unlink   :474-477  有引用时 unlink → -1/EINTR 且置 unlinkflag=TRUE
 *     DoMqueueOpen :239-240  unlinkflag=TRUE 的队列再 open(无 O_CREAT) → -1/EINVAL
 *
 * 断言口径:
 *   mq_open(O_CREAT|O_RDWR) 成功后:
 *   - mq_unlink(同名) == -1 且 errno==EINTR(引用存在, 仅置删除标记)
 *   - mq_open(同名, O_RDWR 无 O_CREAT) == (mqd_t)-1 且 errno==EINVAL(标记生效)
 *   收尾: mq_close(最后引用) 触发真实删除(无等待者)返回 0, 资源回收
 */

/* 用例简要描述: unlink 后同名队列打开被拒 */
#include "It_posix_queue.h"

static UINT32 Testcase(VOID)
{
    CHAR mqname[MQUEUE_STANDARD_NAME_LENGTH] = "";
    mqd_t mqdes;
    INT32 ret;

    ret = snprintf_s(mqname, MQUEUE_STANDARD_NAME_LENGTH, MQUEUE_STANDARD_NAME_LENGTH - 1,
                     "/mq054_%d", LOS_CurTaskIDGet());
    ICUNIT_ASSERT_NOT_EQUAL(ret, -1, ret);

    mqdes = mq_open(mqname, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, NULL);
    ICUNIT_GOTO_NOT_EQUAL(mqdes, (mqd_t)-1, (UINTPTR)mqdes, EXIT1);

    /* 有引用 unlink: 置 unlinkflag, 返回 EINTR */
    errno = 0;
    ret = mq_unlink(mqname);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EINTR, errno, EXIT);

    /* unlinkflag 已置: 同名再 open(无 O_CREAT)被拒 */
    errno = 0;
    mqd_t mqdes2 = mq_open(mqname, O_RDWR);
    ICUNIT_GOTO_EQUAL(mqdes2, (mqd_t)-1, (UINTPTR)mqdes2, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT);

    /* 最后引用关闭触发真实删除(无等待者) */
    ret = mq_close(mqdes);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT1);
    return LOS_OK;
EXIT:
    (VOID)mq_close(mqdes);
EXIT1:
    return LOS_NOK;
}

VOID ItPosixQueue054(VOID)
{
    TEST_ADD_CASE("ItPosixQueue054", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
