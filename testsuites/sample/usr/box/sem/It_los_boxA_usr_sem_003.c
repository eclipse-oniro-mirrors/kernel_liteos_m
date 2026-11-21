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

#include "osTest.h"
#include "syscalls.h"

#if (LOSCFG_TEST_KERNEL_SEM == 1)

#ifdef LOS_SemGetValue
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

#define BOX_SEM003_INIT_COUNT  5U

static UINT32 Sem003Case(VOID)
{
    UINT32 sem = 0;
    INT32 currVal = -1;
    UINT32 ret;

    ret = LOS_SemCreate((UINT16)BOX_SEM003_INIT_COUNT, &sem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SemGetValue(sem, &currVal);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(currVal, 5, (UINT32)currVal);

    ret = LOS_SemPend(sem, LOS_NO_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SemGetValue(sem, &currVal);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(currVal, 4, (UINT32)currVal);

    ret = LOS_SemPost(sem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SemGetValue(sem, &currVal);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(currVal, 5, (UINT32)currVal);

    ret = LOS_SemGetValue(sem, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SEM_PTR_NULL, ret);

    ret = LOS_SemGetValue(0xFFFFFFFFU, &currVal);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SEM_INVALID, ret);

    ret = LOS_SemDelete(sem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SemGetValue(sem, &currVal);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SEM_INVALID, ret);

    return LOS_OK;
}

VOID ItLosBoxUsrSem003(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrSem003", Sem003Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL0, TEST_FUNCTION);
}

#endif /* LOSCFG_TEST_KERNEL_SEM */
