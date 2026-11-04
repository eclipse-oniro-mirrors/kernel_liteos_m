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


#include "osTest.h"
#include "It_los_queue.h"

/* 用例简要描述: 队列防 ABA 行为验证(id 不复用+旧 ID 操作失败) */
static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 queueId1;
    UINT32 queueId2;
    CHAR buff[QUEUE_BASE_MSGSIZE] = "IPCABA01";

    /* Delete 尾插回收, Create 取链头, 不复用同 index */
    ret = LOS_QueueCreate("Q1", QUEUE_BASE_NUM, &queueId1, 0, QUEUE_BASE_MSGSIZE);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_QueueDelete(queueId1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_QueueCreate("Q2", QUEUE_BASE_NUM, &queueId2, 0, QUEUE_BASE_MSGSIZE);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 防复用断言: 新句柄 != 旧句柄 */
    ICUNIT_GOTO_NOT_EQUAL(queueId2, queueId1, queueId2, EXIT);

    /* 旧句柄操作应失败(CB 已回收, state=LOS_UNUSED) */
    ret = LOS_QueueWriteCopy(queueId1, buff, QUEUE_BASE_MSGSIZE, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_QUEUE_NOT_CREATE, ret, EXIT);

    ret = LOS_QueueDelete(queueId2);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    return LOS_OK;

EXIT:
    (VOID)LOS_QueueDelete(queueId2);
    return LOS_OK;
}

VOID ItLosQueue301(VOID)
{
    TEST_ADD_CASE("ItLosQueue301", Testcase, TEST_LOS, TEST_FN_KERNEL_MSGQUEUE, TEST_LEVEL1, TEST_FUNCTION);
}
