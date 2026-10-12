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
 * 用例名称: ItPosixQueue053
 * 用例描述: 消息队列空名字参数拒绝
 *
 * 覆盖目标:
 *   lib/posix/src/mqueue.c:
 *     MqNameCheck :81-82  mqName==NULL → errno=EINVAL, return -1
 *     (mq_open/mq_unlink 入口统一走 MqNameCheck, 现有 52 例无 NULL 名用例)
 *
 * 断言口径:
 *   mq_open(NULL, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR, NULL) == (mqd_t)-1 且 errno==EINVAL
 *   mq_unlink(NULL) == -1 且 errno==EINVAL
 */

/* 用例简要描述: 消息队列空名字参数拒绝 */
#include "It_posix_queue.h"

static UINT32 Testcase(VOID)
{
    mqd_t mqdes;

    errno = 0;
    mqdes = mq_open(NULL, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, NULL);
    ICUNIT_ASSERT_EQUAL(mqdes, (mqd_t)-1, (UINTPTR)mqdes);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    errno = 0;
    INT32 ret = mq_unlink(NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    return LOS_OK;
}

VOID ItPosixQueue053(VOID)
{
    TEST_ADD_CASE("ItPosixQueue053", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
