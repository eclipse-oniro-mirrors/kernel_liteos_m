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

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 level;

    ret = LOS_HplSetLogLevel(HPL_TEST_LOG_LEVEL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    level = OsHplGetLogLevel();
    ICUNIT_ASSERT_EQUAL(level, HPL_TEST_LOG_LEVEL, level);

    ret = LOS_HplSetLogLevel(HPL_SILENT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    level = OsHplGetLogLevel();
    ICUNIT_ASSERT_EQUAL(level, HPL_SILENT, level);

    /* Restore the level for the following record cases. */
    ret = LOS_HplSetLogLevel(HPL_TEST_LOG_LEVEL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

VOID ItLosHpl001(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHpl001", TestCase, TEST_LOS, TEST_HPL, TEST_LEVEL0, TEST_FUNCTION);
}
