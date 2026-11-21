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

static volatile UINT32 g_mux006State;
static volatile UINT32 g_mux006Held;
static UINT32 g_mux006Mux;

static VOID Mux006Probe(VOID)
{
    g_mux006State = 1;
    g_mux006Held = LOS_MuxHeld(g_mux006Mux);
    g_mux006State = 2;
}

static UINT32 Mux006Case(VOID)
{
    UINT32 mux = 0;
    UINT32 child = 0;
    UINT32 ret;

    ret = LOS_MuxCreate(&mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_MuxHeld(mux);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    g_mux006Mux = mux;
    g_mux006State = 0;
    g_mux006Held = 0xFFFFFFFFU;
    ret = BoxMuxSpawn((TSK_ENTRY_FUNC)Mux006Probe, "mux006p", &child);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(BoxMuxWaitState(&g_mux006State, 2U), TRUE, g_mux006State);
    ICUNIT_ASSERT_EQUAL(g_mux006Held, 0, g_mux006Held);
    BoxMuxReap();

    ret = LOS_MuxPend(mux, LOS_NO_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    g_mux006State = 0;
    g_mux006Held = 0xFFFFFFFFU;
    ret = BoxMuxSpawn((TSK_ENTRY_FUNC)Mux006Probe, "mux006q", &child);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(BoxMuxWaitState(&g_mux006State, 2U), TRUE, g_mux006State);
    ICUNIT_ASSERT_EQUAL(g_mux006Held, 1, g_mux006Held);
    BoxMuxReap();

    ret = LOS_MuxPost(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_MuxHeld(mux);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = LOS_MuxHeld(0xFFFFFFFFU);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = LOS_MuxDelete(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_MuxHeld(mux);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    return LOS_OK;
}

VOID ItLosBoxUsrMux006(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMux006", Mux006Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL0, TEST_FUNCTION);
}

#endif /* LOSCFG_TEST_KERNEL_MUX */
