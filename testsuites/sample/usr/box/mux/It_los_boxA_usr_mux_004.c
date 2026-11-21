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

#define BOX_MUX_POOL_MAX  16U

static UINT32 g_mux004Handles[BOX_MUX_POOL_MAX];

static UINT32 Mux004Case(VOID)
{
    UINT32 count = 0;
    UINT32 probe = 0;
    UINT32 i;
    UINT32 ret;

    for (i = 0; i < BOX_MUX_POOL_MAX; i++) {
        ret = LOS_MuxCreate(&g_mux004Handles[i]);
        if (ret != LOS_OK) {
            break;
        }
        count++;
    }
    ICUNIT_ASSERT_NOT_EQUAL(count, 0, count);

    ret = LOS_MuxCreate(&probe);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_MUX_ALL_BUSY, ret);

    for (i = 0; i < count; i++) {
        ret = LOS_MuxPend(g_mux004Handles[i], 0);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

        ret = LOS_MuxPost(g_mux004Handles[i]);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    }

    ret = LOS_MuxDelete(g_mux004Handles[0]);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    g_mux004Handles[0] = 0;

    ret = LOS_MuxCreate(&probe);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    g_mux004Handles[0] = probe;

    for (i = 0; i < BOX_MUX_POOL_MAX; i++) {
        if (g_mux004Handles[i] != 0U) {
            (VOID)LOS_MuxDelete(g_mux004Handles[i]);
            g_mux004Handles[i] = 0U;
        }
    }

    return LOS_OK;
}

VOID ItLosBoxUsrMux004(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMux004", Mux004Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_TEST_KERNEL_MUX */
