/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ItLosBox004 - LOS_QueueInfoAllGet: export the queue
 * header plus the wait-task bitmap (READ/WRITE/MEM segments).
 * Phases:
 *  1. a too-small buffer is rejected;
 *  2. header fields match a known queue state (len/size/readable/writable);
 *  3. a parked reader shows up at its taskId bit in the READ segment and
 *     the bit clears once the reader is served - the export is polled with
 *     InfoAllGet itself instead of a fixed delay (fixed delays race
 *     against higher-priority load).
 * Kernel mode, no box context. */

#include "osTest.h"
#include "It_los_box.h"
#include "los_queue_pri.h"   /* queue API + bitmap macros */

#define BOX_Q_LEN       3   /* queue depth */
#define BOX_Q_MSGSIZE   8   /* message payload size */

static UINT32 g_infoSid;
static UINT32 g_readerTaskId;

static VOID TaskF004(VOID)
{
    CHAR buff[BOX_Q_MSGSIZE] = "";
    UINT32 size = BOX_Q_MSGSIZE;

    (VOID)LOS_QueueReadCopy(g_infoSid, buff, &size, LOS_WAIT_FOREVER);
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 poll;
    UINT32 expectBit;
    UINT32 readSize;
    CHAR buff1[BOX_Q_MSGSIZE] = "UniDSP";
    CHAR buff2[BOX_Q_MSGSIZE] = "";
    static UINT8 infoBuf[QUEUE_ALL_INFO_SIZE(LOSCFG_BASE_CORE_TSK_LIMIT)];
    QUEUE_HEAD_INFO_S *info = (QUEUE_HEAD_INFO_S *)infoBuf;
    TSK_INIT_PARAM_S taskParam = { 0 };

    g_infoSid = 0;
    ret = LOS_QueueCreate("IQ1", BOX_Q_LEN, &g_infoSid, 0, BOX_Q_MSGSIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 1. too-small buffer rejected (header alone is not enough) */
    ret = LOS_QueueInfoAllGet(g_infoSid, LOSCFG_BASE_CORE_TSK_LIMIT, info,
                              sizeof(QUEUE_HEAD_INFO_S) - 1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_QUEUE_INFO_SIZE_TOO_SMALL, ret);

    /* 2. header matches a known state: one message written */
    ret = LOS_QueueWriteCopy(g_infoSid, buff1, BOX_Q_MSGSIZE, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_QueueInfoAllGet(g_infoSid, LOSCFG_BASE_CORE_TSK_LIMIT, info, sizeof(infoBuf));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(info->usQueueLen, BOX_Q_LEN, info->usQueueLen);
    ICUNIT_ASSERT_EQUAL(info->usQueueSize, BOX_Q_MSGSIZE + sizeof(UINT32), info->usQueueSize);
    ICUNIT_ASSERT_EQUAL(info->usReadableCnt, 1, info->usReadableCnt);
    ICUNIT_ASSERT_EQUAL(info->usWritableCnt, BOX_Q_LEN - 1, info->usWritableCnt);
    /* nobody is waiting yet: the READ segment word for our tasks is zero */
    ICUNIT_ASSERT_EQUAL(info->usWaitTask[GET_READ_ARRAY_IDX(0, LOSCFG_BASE_CORE_TSK_LIMIT)], 0,
                        info->usWaitTask[GET_READ_ARRAY_IDX(0, LOSCFG_BASE_CORE_TSK_LIMIT)]);

    /* drain the message so the reader will park */
    readSize = BOX_Q_MSGSIZE;
    ret = LOS_QueueReadCopy(g_infoSid, buff2, &readSize, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 3. parked reader observed at its taskId bit, then cleared after wake.
     * A lower-priority task (larger priority number): it only runs while
     * the test task sleeps. */
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF004;
    taskParam.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    taskParam.pcName = "BoxReader004";
    taskParam.usTaskPrio = TASK_PRIO_TEST + 1;   /* below IT_TST_INI */
    ret = LOS_TaskCreate(&g_readerTaskId, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    expectBit = (UINT32)GET_ARRAY_BIT(g_readerTaskId);
    ret = LOS_NOK;
    for (poll = 0; poll < 100; poll++) {          /* poll the bitmap itself */
        (VOID)LOS_TaskDelay(2);
        ret = LOS_QueueInfoAllGet(g_infoSid, LOSCFG_BASE_CORE_TSK_LIMIT, info, sizeof(infoBuf));
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        if ((info->usWaitTask[GET_READ_ARRAY_IDX(g_readerTaskId, LOSCFG_BASE_CORE_TSK_LIMIT)]
             & expectBit) != 0) {
            break;
        }
    }
    ICUNIT_ASSERT_NOT_EQUAL(poll, 100, poll);    /* reader must have parked */

    /* serve the reader and let it settle: the bit must clear */
    ret = LOS_QueueWriteCopy(g_infoSid, buff1, BOX_Q_MSGSIZE, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    for (poll = 0; poll < 100; poll++) {
        (VOID)LOS_TaskDelay(2);
        ret = LOS_QueueInfoAllGet(g_infoSid, LOSCFG_BASE_CORE_TSK_LIMIT, info, sizeof(infoBuf));
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        if ((info->usWaitTask[GET_READ_ARRAY_IDX(g_readerTaskId, LOSCFG_BASE_CORE_TSK_LIMIT)]
             & expectBit) == 0) {
            break;
        }
    }
    ICUNIT_ASSERT_NOT_EQUAL(poll, 100, poll);    /* reader must have been served */

    (VOID)LOS_TaskDelete(g_readerTaskId);        /* reclaim the exited task */
    ret = LOS_QueueDelete(g_infoSid);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

VOID ItLosBox004(VOID)
{
    TEST_ADD_CASE("ItLosBox004", TestCase, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
}
