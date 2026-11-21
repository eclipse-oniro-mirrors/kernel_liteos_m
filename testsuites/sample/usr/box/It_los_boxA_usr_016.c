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
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ItLosBoxUsr016 - scenario 2: cross-domain task id operation is rejected by
 * the unified check before any TCB access.
 *
 * Runs in boxA USER mode. A box task id is encoded as (bid + 1) << 16 | slot;
 * kernel task ids have a zero high segment. */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root"
#endif

static UINT32 TestCase(VOID)
{
    UINT32 ret;

    /* 1) kernel task id (idle, slot 0): untrusted context must not reach it */
    ret = LOS_TaskDelete(0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_PERM_ERR, ret);

    /* also the next kernel slot */
    ret = LOS_TaskDelete(1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_PERM_ERR, ret);

    /* 2) a box id beyond the configured box limit: invalid id */
    ret = LOS_TaskDelete((UINT32)4U << 16);   /* bid = 3, above max limit */
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_INVALID, ret);

    /* 3) own box segment but slot outside the pool quota: invalid id */
    ret = LOS_TaskDelete(((UINT32)0U + 1U) << 16 | 0x3F);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_INVALID, ret);

    return LOS_OK;
}

VOID ItLosBoxUsr016(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsr016", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
