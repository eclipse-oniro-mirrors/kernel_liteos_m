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

/* ItLosBox003 - LOS_QueueCreateWithSId: create a queue
 * bound to a PRE-GENERATED sId - the other half of the "interrupts may only
 * touch pre-created sIds" path (LOS_GenSId makes the slot, this binds it).
 * Phases:
 *  1. negative: an sId of the WRONG TYPE (generated as SEM) is refused by
 *     the owner+type check;
 *  2. create over caller-provided message memory through a pre-generated
 *     QUE sId, then write/read THROUGH that same sId - the bind must be
 *     visible to the resolve path every operate API runs through;
 *  3. delete frees the CB and the sId slot; a fresh cycle works.
 * Kernel mode, no box context: the sId is kernel-head owned and the CB
 * comes from the kernel pool (the OHOS g_staticQueue second pool is never
 * involved). */

#include "osTest.h"
#include "It_los_box.h"
#include "los_queue_pri.h"   /* queue API + helpers */
#include "los_sec_pri.h"     /* LOS_GenSId / LOS_SIdDelete / SID types */

#define BOX_Q_LEN       3   /* queue depth */
#define BOX_Q_MSGSIZE   8   /* message payload size */

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 badSid;
    UINT32 sId;
    UINT32 readSize;
    CHAR buff1[BOX_Q_MSGSIZE] = "UniDSP";
    CHAR buff2[BOX_Q_MSGSIZE] = "";
    /* static so the message buffer lives in kernel RAM, not on the 4K test stack */
    static UINT8 queueMem[BOX_Q_LEN * (BOX_Q_MSGSIZE + sizeof(UINT32))];

    /* 1. wrong type: a SEM sId must be rejected by the owner+type check */
    badSid = LOS_GenSId((UINT32)LOS_SID_TYPE_SEM);
    ICUNIT_ASSERT_NOT_EQUAL(badSid, 0, badSid);
    ret = LOS_QueueCreateWithSId(BOX_Q_LEN, badSid, 0, BOX_Q_MSGSIZE, queueMem, sizeof(queueMem));
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SEC_INVALID_SID, ret);
    ret = LOS_SIdDelete(badSid);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 2. pre-generated QUE sId: create + operate through the SAME handle */
    sId = LOS_GenSId((UINT32)LOS_SID_TYPE_QUE);
    ICUNIT_ASSERT_NOT_EQUAL(sId, 0, sId);

    ret = LOS_QueueCreateWithSId(BOX_Q_LEN, sId, 0, BOX_Q_MSGSIZE, queueMem, sizeof(queueMem));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_QueueWriteCopy(sId, buff1, BOX_Q_MSGSIZE, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    readSize = BOX_Q_MSGSIZE;
    ret = LOS_QueueReadCopy(sId, buff2, &readSize, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(readSize, BOX_Q_MSGSIZE, readSize);

    /* 3. delete recycles CB + slot; a full second cycle works */
    ret = LOS_QueueDelete(sId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    sId = LOS_GenSId((UINT32)LOS_SID_TYPE_QUE);
    ICUNIT_ASSERT_NOT_EQUAL(sId, 0, sId);
    ret = LOS_QueueCreateWithSId(BOX_Q_LEN, sId, 0, BOX_Q_MSGSIZE, queueMem, sizeof(queueMem));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_QueueDelete(sId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

VOID ItLosBox003(VOID)
{
    TEST_ADD_CASE("ItLosBox003", TestCase, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
}
