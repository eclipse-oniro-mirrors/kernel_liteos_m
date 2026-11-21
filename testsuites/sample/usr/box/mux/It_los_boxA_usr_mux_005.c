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
#include "los_sec_pri.h"
#include "boxes_conf.h"

#if (LOSCFG_TEST_KERNEL_MUX == 1)

static UINT32 Mux005Case(VOID)
{
    UINT32 mux = 0;
    UINT32 forged = 0;
    UINT32 ret;

    ret = LOS_MuxCreate(&mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = (UINT32)OS_GET_SID_BOXID(mux);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)LOS_BOXA_ID, ret);

    ret = (UINT32)OS_GET_SID_TYPE(mux);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)LOS_SID_TYPE_MUX, ret);

    forged = (mux & ~OS_SID_BOXID_MASK) | ((UINT32)LOS_BOXB_ID << OS_SID_BOX_SHIFT);

    ret = LOS_MuxPend(forged, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_MUX_INVALID, ret);

    ret = LOS_MuxPost(forged);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_MUX_INVALID, ret);

    ret = LOS_MuxDelete(forged);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_MUX_INVALID, ret);

    ret = LOS_MuxPend(mux, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_MuxHeld(mux);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);

    ret = LOS_MuxPost(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_MuxDelete(mux);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

VOID ItLosBoxUsrMux005(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMux005", Mux005Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_TEST_KERNEL_MUX */
