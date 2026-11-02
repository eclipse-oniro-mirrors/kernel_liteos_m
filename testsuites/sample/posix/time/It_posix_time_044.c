/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 用例名称: ItPosixTime044
 * 用例描述: 64 位时间转换溢出错误码与 2038 后正路径
 *
 * 覆盖目标:
 *   lib/posix/src/time64.c:
 *     gmtime64_r     :188-189  __secs_to_tm 64 位输入溢出 → NULL + EOVERFLOW
 *     (localtime64_r :90-93 前置范围检查同口径一并覆盖)
 *   third_party/musl __secs_to_tm: t > INT_MAX*31622400(≈6.79e16) → -1
 *
 * 断言口径(设计矩阵适配说明):
 *   - gmtime64_r((time64_t)1e17) == NULL 且 errno==EOVERFLOW
 *     (矩阵原文 1e12 实测不溢出: 1e12s≈公元33658年仍在 __secs_to_tm int 年份
 *      限内; 溢出阈值 ≈6.79e16s, 改用 1e17)
 *   - localtime64_r((time64_t)1e17) == NULL 且 errno==EOVERFLOW
 *   - 正路径(2038 后): gmtime64_r(1e12) 成功且 tm_year==31758(公元33658);
 *     mktime64 往返 == 1e12 + timezone(时区往返一致)
 *   - mktime64 溢出: 构造 tm_year=tm_mon=INT_MAX(未归一字段, __tm_to_secs 归一
 *     后年份超过 __secs_to_tm 阈值) → -1 且 errno==EOVERFLOW(矩阵原文
 *     "tm_year=30000→EOVERFLOW" 实测不达阈值, 30000 年正常返回)
 */

/* 用例简要描述: 64 位时间转换溢出错误码 */
#include "It_posix_time.h"
#include <limits.h>
#include "time64.h"

#define TIME044_BIG     (100000000000000000LL)  /* 1e17: > __secs_to_tm 阈值 6.79e16 */
#define TIME044_YEAR3K  (1000000000000LL)       /* 1e12: ≈公元 33658 年, 2038 后正路径 */
#define TIME044_TM_YEAR_33658 31758             /* 33658 - 1900 */

extern long timezone;

static UINT32 Testcase(VOID)
{
    struct tm tmOut = {0};
    struct tm *tmRet = NULL;
    time64_t t;
    time64_t tRet;

    /* 1. gmtime64_r 溢出: :188-189 → NULL/EOVERFLOW */
    t = (time64_t)TIME044_BIG;
    errno = 0;
    tmRet = gmtime64_r(&t, &tmOut);
    ICUNIT_ASSERT_EQUAL((UINTPTR)tmRet, (UINTPTR)NULL, (UINTPTR)tmRet);
    ICUNIT_ASSERT_EQUAL(errno, EOVERFLOW, errno);

    /* 2. localtime64_r 前置范围检查同口径 → NULL/EOVERFLOW */
    errno = 0;
    tmRet = localtime64_r(&t, &tmOut);
    ICUNIT_ASSERT_EQUAL((UINTPTR)tmRet, (UINTPTR)NULL, (UINTPTR)tmRet);
    ICUNIT_ASSERT_EQUAL(errno, EOVERFLOW, errno);

    /* 3. 2038 后正路径: 1e12 秒 ≈ 公元 33658-05-18, tm_year==31758 */
    t = (time64_t)TIME044_YEAR3K;
    (VOID)memset_s(&tmOut, sizeof(tmOut), 0, sizeof(tmOut));
    tmRet = gmtime64_r(&t, &tmOut);
    ICUNIT_ASSERT_EQUAL((UINTPTR)tmRet, (UINTPTR)&tmOut, (UINTPTR)tmRet);
    ICUNIT_ASSERT_EQUAL(tmOut.tm_year, TIME044_TM_YEAR_33658, tmOut.tm_year);

    /* 4. mktime64 往返: UTC 字段回转 == 原值 + timezone(西正时区偏移, 秒级精确) */
    tRet = mktime64(&tmOut);
    ICUNIT_ASSERT_NOT_EQUAL(tRet, (time64_t)-1, (INT64)tRet);
    ICUNIT_ASSERT_EQUAL((INT64)(tRet - t), (INT64)timezone, (INT64)(tRet - t));

    /* 5. mktime64 溢出: tm_year=tm_mon=INT_MAX → 归一后超 __secs_to_tm 阈值 → EOVERFLOW */
    (VOID)memset_s(&tmOut, sizeof(tmOut), 0, sizeof(tmOut));
    tmOut.tm_year = INT_MAX;
    tmOut.tm_mon = INT_MAX; /* 未归一字段: __tm_to_secs 年份抬升至 ~23.3 亿年 */
    tmOut.tm_mday = 1;
    errno = 0;
    tRet = mktime64(&tmOut);
    ICUNIT_ASSERT_EQUAL((INT64)tRet, (INT64)-1, (INT64)tRet);
    ICUNIT_ASSERT_EQUAL(errno, EOVERFLOW, errno);

    return LOS_OK;
}

VOID ItPosixTime044(VOID)
{
    TEST_ADD_CASE("ItPosixTime044", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
