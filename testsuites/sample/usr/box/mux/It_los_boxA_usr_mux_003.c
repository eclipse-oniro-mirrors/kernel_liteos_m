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
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
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

#include "It_los_boxA_usr_mux_util.h"

#if (LOSCFG_TEST_KERNEL_MUX == 1)

static volatile UINT32 g_mux003State;
static volatile UINT32 g_mux003Release;
static volatile UINT32 g_mux003PendRet;
static volatile UINT32 g_mux003PostRet;
static UINT32 g_mux003Mux;

static VOID Mux003Holder(VOID)
{
    g_mux003State = 1;
    g_mux003PendRet = LOS_MuxPend(g_mux003Mux, LOS_WAIT_FOREVER);
    if (g_mux003PendRet != LOS_OK) {
        g_mux003State = 3;
        return;
    }

    g_mux003State = 2;
    while (g_mux003Release == 0U) {
        LOS_TaskDelay(1);
    }

    g_mux003PostRet = LOS_MuxPost(g_mux003Mux);
    g_mux003State = 3;
}

static UINT32 Mux003Case(VOID)
{
    UINT32 mux = 0;
    UINT32 child = 0;
    UINT32 ret;

    ret = LOS_MuxCreate(&mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    g_mux003Mux = mux;
    g_mux003Release = 0;
    g_mux003State = 0;
    g_mux003PendRet = 0xFFFFFFFFU;
    g_mux003PostRet = 0xFFFFFFFFU;

    ret = LOS_MuxPend(mux, LOS_WAIT_FOREVER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = BoxMuxSpawn((TSK_ENTRY_FUNC)Mux003Holder, "mux003h", &child);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(BoxMuxWaitState(&g_mux003State, 1U), TRUE, g_mux003State);

    ret = LOS_MuxPost(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(BoxMuxWaitState(&g_mux003State, 2U), TRUE, g_mux003State);

    ret = LOS_MuxPost(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_MUX_INVALID, ret);

    ret = LOS_MuxDelete(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_MUX_PENDED, ret);

    ret = LOS_MuxHeld(mux);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);

    g_mux003Release = 1U;

    ICUNIT_ASSERT_EQUAL(BoxMuxWaitState(&g_mux003State, 3U), TRUE, g_mux003State);
    ICUNIT_ASSERT_EQUAL(g_mux003PendRet, LOS_OK, g_mux003PendRet);
    ICUNIT_ASSERT_EQUAL(g_mux003PostRet, LOS_OK, g_mux003PostRet);

    BoxMuxReap();

    ret = LOS_MuxDelete(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

VOID ItLosBoxUsrMux003(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMux003", Mux003Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_TEST_KERNEL_MUX */
