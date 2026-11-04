/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "It_los_hpl.h"

/* Bigger than the on-stack custom buffer threshold so the dynamic-allocation
 * path of LOS_HplRecordCustomString is exercised as well. */
#define HPL_TEST_BIG_STR_SIZE 1000

static CHAR g_hplCustomBuf[] = "hpl custom string";

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 calls;
    UINT32 callsNow;
    UINT32 i;
    CHAR bigBuf[HPL_TEST_BIG_STR_SIZE];

    TestHplPrepare();

    ret = LOS_HplRecordCustomString(NULL, sizeof(g_hplCustomBuf));
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HPL_PARAM_INVALID, ret);

    ret = LOS_HplRecordCustomString(g_hplCustomBuf, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HPL_PARAM_INVALID, ret);

    /* The record hook is invoked synchronously by OsHplMetadataRecord. */
    calls = g_hplHookCalls;
    ret = LOS_HplRecordCustomString(g_hplCustomBuf, sizeof(g_hplCustomBuf));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    callsNow = g_hplHookCalls;
    ICUNIT_ASSERT_EQUAL(callsNow, calls + 1, callsNow - calls);

    for (i = 0; i < (sizeof(bigBuf) - 1); i++) {
        bigBuf[i] = 'x';
    }
    bigBuf[sizeof(bigBuf) - 1] = '\0';
    calls = g_hplHookCalls;
    ret = LOS_HplRecordCustomString(bigBuf, sizeof(bigBuf));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    callsNow = g_hplHookCalls;
    ICUNIT_ASSERT_EQUAL(callsNow, calls + 1, callsNow - calls);

    return LOS_OK;
}

VOID ItLosHpl003(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHpl003", TestCase, TEST_LOS, TEST_HPL, TEST_LEVEL0, TEST_FUNCTION);
}
