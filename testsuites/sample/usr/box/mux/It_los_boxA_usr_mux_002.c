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

static volatile UINT32 g_mux002State;
static volatile UINT32 g_mux002Result;
static UINT32 g_mux002Mux;

static VOID Mux002WaiterForever(VOID)
{
    g_mux002State = 1;
    g_mux002Result = LOS_MuxPend(g_mux002Mux, LOS_WAIT_FOREVER);
    if (g_mux002Result == LOS_OK) {
        g_mux002Result = LOS_MuxPost(g_mux002Mux);
    }
    g_mux002State = 2;
}

static VOID Mux002WaiterTimeout(VOID)
{
    g_mux002State = 1;
    g_mux002Result = LOS_MuxPend(g_mux002Mux, BOX_MUX_TIMEOUT_TICKS);
    g_mux002State = 2;
}

static UINT32 Mux002Case(VOID)
{
    UINT32 mux = 0;
    UINT32 child = 0;
    UINT32 ret;

    ret = LOS_MuxCreate(&mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    g_mux002Mux = mux;

    ret = LOS_MuxPend(mux, LOS_WAIT_FOREVER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    g_mux002State = 0;
    g_mux002Result = 0xFFFFFFFFU;
    ret = BoxMuxSpawn((TSK_ENTRY_FUNC)Mux002WaiterForever, "mux002w", &child);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(BoxMuxWaitState(&g_mux002State, 1U), TRUE, g_mux002State);
    ICUNIT_ASSERT_EQUAL(g_mux002State, 1, g_mux002State);

    ret = LOS_MuxHeld(mux);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);

    ret = LOS_MuxPost(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(BoxMuxWaitState(&g_mux002State, 2U), TRUE, g_mux002State);
    ICUNIT_ASSERT_EQUAL(g_mux002Result, LOS_OK, g_mux002Result);
    BoxMuxReap();

    ret = LOS_MuxPend(mux, LOS_WAIT_FOREVER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    g_mux002State = 0;
    g_mux002Result = 0xFFFFFFFFU;
    ret = BoxMuxSpawn((TSK_ENTRY_FUNC)Mux002WaiterTimeout, "mux002t", &child);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(BoxMuxWaitState(&g_mux002State, 2U), TRUE, g_mux002State);
    ICUNIT_ASSERT_EQUAL(g_mux002Result, LOS_ERRNO_MUX_TIMEOUT, g_mux002Result);

    ret = LOS_MuxHeld(mux);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);

    BoxMuxReap();

    ret = LOS_MuxPost(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_MuxDelete(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

VOID ItLosBoxUsrMux002(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMux002", Mux002Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_TEST_KERNEL_MUX */
