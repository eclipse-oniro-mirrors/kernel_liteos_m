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

/* ============================================================================
 * It_los_sem_046.c - doc 07, 4.2.1 baseline regression, non-FULL set.
 *
 * The binary-semaphore and query cases (301..304) are behind
 * LOS_KERNEL_TEST_FULL, so the default test.config never moves the count 3
 * path or the binary overflow path. This case adds the missing pieces to the
 * always-run set, using only the public API:
 *
 *   - counting semaphore created with count 3: three pends drain it, the
 *     fourth non-blocking pend returns SEM_UNAVAILABLE, three posts refill it;
 *   - deleting it leaves a stale sId: pend/post/delete all report SEM_INVALID;
 *   - re-creating on the recycled slot works (pool / entity recycle);
 *   - binary semaphore: pend then post; a second post returns SEM_OVERFLOW
 *     (maxCount is 1, unlike the counting object above).
 *
 * LOS_SemGetValue is intentionally not called here: in the BOX/SID build it
 * still reads the raw pool index while create returns an sId, so the query
 * interface needs its own SID-resolution fix before it can be asserted.
 * ========================================================================== */

#include "It_los_sem.h"

#define SEM046_INIT_COUNT   3U

static UINT32 Sem046Case(VOID)
{
    UINT32 sem = 0;
    UINT32 binSem = 0;
    UINT32 i;
    UINT32 ret;
    UINT32 fails = 0;

    /* ---- counting semaphore with the doc's initial count 3 ---- */
    ret = LOS_SemCreate((UINT16)SEM046_INIT_COUNT, &sem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    for (i = 0; i < SEM046_INIT_COUNT; i++) {
        ret = LOS_SemPend(sem, LOS_NO_WAIT);
        if (ret != LOS_OK) {
            fails++;
        }
    }

    ret = LOS_SemPend(sem, LOS_NO_WAIT);
    if (ret != LOS_ERRNO_SEM_UNAVAILABLE) {
        fails++;                         /* count must be exactly zero */
    }

    for (i = 0; i < SEM046_INIT_COUNT; i++) {
        ret = LOS_SemPost(sem);
        if (ret != LOS_OK) {
            fails++;
        }
    }

    ret = LOS_SemDelete(sem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* ---- stale sId after delete ---- */
    if (LOS_SemPend(sem, LOS_NO_WAIT) != LOS_ERRNO_SEM_INVALID) {
        fails++;
    }
    if (LOS_SemPost(sem) != LOS_ERRNO_SEM_INVALID) {
        fails++;
    }
    if (LOS_SemDelete(sem) != LOS_ERRNO_SEM_INVALID) {
        fails++;
    }

    /* ---- the recycled slot must be usable again ---- */
    ret = LOS_SemCreate(0, &sem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SemDelete(sem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* ---- binary semaphore: maxCount 1, over-post is an error ---- */
    ret = LOS_BinarySemCreate(1, &binSem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SemPend(binSem, LOS_NO_WAIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SemPost(binSem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SemPost(binSem);
    if (ret != LOS_ERRNO_SEM_OVERFLOW) {
        fails++;
    }

    ret = LOS_SemDelete(binSem);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosSem046(void)
{
    TEST_ADD_CASE("ItLosSem046", Sem046Case, TEST_LOS, TEST_SEM, TEST_LEVEL0, TEST_FUNCTION);
}
