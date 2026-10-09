/*
 * Copyright (c) 2022-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ItFuzzPosixMq: mq_open/getattr/setattr/send/receive/close/unlink */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_posix_mqueue_fuzz.h"

static void *FzPosixThreadEntry(void *arg) __attribute__((unused));
static void *FzPosixThreadEntry(void *arg) { (VOID)arg; return NULL; }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    mqd_t mq; CHAR name[16]; CHAR msg[8] = {0}; struct mq_attr attr;
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        (VOID)snprintf(name, sizeof(name), "/fzq%d", i % 8);
        mq = mq_open(name, O_CREAT | O_RDWR, 0644, NULL);
        if (mq >= 0) {
            (VOID)mq_getattr(mq, &attr);
            attr.mq_flags = (*(INT32 *)DT_SetGetS32(&g_element[0], 0)) & 1 ? O_NONBLOCK : 0;
            (VOID)mq_setattr(mq, &attr, NULL);
            (VOID)mq_send(mq, msg, 1, 0); (VOID)mq_receive(mq, msg, sizeof(msg), NULL);
            (VOID)mq_close(mq); (VOID)mq_unlink(name);
        }
    }

    /* directed: full-queue EAGAIN + drain receive */
    {
        struct mq_attr a2 = {0};
        a2.mq_maxmsg = 1;
        a2.mq_msgsize = 8;
        mqd_t m2 = mq_open("/fzfull", O_CREAT | O_RDWR | O_NONBLOCK, 0644, &a2);
        if (m2 >= 0) {
            char b2[8] = {0};
            (VOID)mq_send(m2, b2, 8, 0);
            (VOID)mq_send(m2, b2, 8, 0);   /* full -> EAGAIN */
            (VOID)mq_receive(m2, b2, sizeof(b2), NULL);
            (VOID)mq_close(m2);
            (VOID)mq_unlink("/fzfull");
        }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzPosixMq(VOID)
{
    TEST_ADD_CASE("ItFuzzPosixMq", TestCase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
