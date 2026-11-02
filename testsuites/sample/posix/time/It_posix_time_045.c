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
 * 用例名称: ItPosixTime045
 * 用例描述: 64 位本地时间(localtime64/mktime64)的 DST 校正路径
 *
 * 覆盖目标:
 *   lib/posix/src/time64.c:
 *     localtime64_r :82        CheckWithinDstPeriod 命中 → DstForwardSecondGet
 *                               (039 只测了 time.c 的 localtime_r, 64 位版未覆盖)
 *     mktime64     :156-159   tm_isdst!=0 且在 DST 区间 → 取生效秒并清 isdst,
 *                               :167 t = t + tzoff - dstsec
 *
 * 断言口径(DST 规则: Mar-01 00:00 起, Oct-01 00:00 止, 前拨 3600s, 2026 年):
 *   期内(2026-06-15) isdst=0 基准 t0(=12:00 本地标准时的 UTC 时刻):
 *   - localtime64_r(t0): tm_isdst==1, tm_hour==13(DST 墙钟=标准时+前拨量,
 *     与 glibc 语义一致: isdst=0 的 mktime 输入按标准时解释, 期内墙钟已
 *     前拨 1h), __tm_gmtoff == (DST 关闭时 gmtoff) + 3600
 *   - localtime64(t0)(非 _r 版, 静态缓冲): tm_isdst==1
 *   - mktime64(同字段 isdst=1) == t0 - 3600
 *   恢复: dst_disable 后 gmtoff 回到非 DST 值
 *
 * 实现语义偏离说明(对比初版):
 *   初版断言 dstOn.tm_hour==12, 实测为 13(Errline:87 RetCode:0xd)。
 *   核对 time64.c: mktime64(isdst=0) 将输入按本地标准时折算 UTC,
 *   localtime64_r 在 DST 期内按 墙钟=标准+前拨量 显示(timeoff =
 *   t - tzoff + dstsec), 12:00 标准时显示为 13:00——实现语义自洽且与
 *   glibc 一致, 初版期望值错误, 修正为 13。
 */

/* 用例简要描述: 64 位本地时间夏令时校正 */
#include "It_posix_time.h"
#include "time64.h"
#include "tzdst.h"

#define DST045_START_STR   "Mar-01 00:00:00"
#define DST045_END_STR     "Oct-01 00:00:00"
#define DST045_FORWARD_SEC 3600
#define DST045_YEAR_TM     126 /* 2026 */

static UINT32 Testcase(VOID)
{
    struct tm inTm = {0};
    struct tm dstOn = {0};
    struct tm dstOff = {0};
    struct tm *tmRet = NULL;
    time64_t t0;
    time64_t tDst;

    /* 注入 DST 规则(与 039 同规则) */
    INT32 ret = dst_enable(DST045_START_STR, DST045_END_STR, DST045_FORWARD_SEC);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 期内基准: 2026-06-15 12:00:00 本地标准时, isdst=0 */
    inTm.tm_year = DST045_YEAR_TM;
    inTm.tm_mon = 5;    /* 5: June */
    inTm.tm_mday = 15;
    inTm.tm_hour = 12;
    inTm.tm_isdst = 0;
    t0 = mktime64(&inTm);
    ICUNIT_GOTO_NOT_EQUAL(t0, (time64_t)-1, (INT64)t0, EXIT);

    /* localtime64_r: 期内 → dstsec=3600, isdst==1, 墙钟=12:00 标准时+1h */
    tmRet = localtime64_r(&t0, &dstOn);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)tmRet, (UINTPTR)NULL, (UINTPTR)tmRet, EXIT);
    ICUNIT_GOTO_EQUAL(dstOn.tm_isdst, 1, dstOn.tm_isdst, EXIT);
    ICUNIT_GOTO_EQUAL(dstOn.tm_hour, 13, dstOn.tm_hour, EXIT); /* 13: DST 墙钟 12+1h */

    /* localtime64(非 _r 版, :105-107 静态缓冲): 期内同样 isdst==1 */
    tmRet = localtime64(&t0);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)tmRet, (UINTPTR)NULL, (UINTPTR)tmRet, EXIT);
    ICUNIT_GOTO_EQUAL(tmRet->tm_isdst, 1, tmRet->tm_isdst, EXIT);

    /* mktime64(isdst=1): :156-159 取生效秒, :167 减前拨量 → t0-3600 */
    inTm.tm_year = DST045_YEAR_TM;
    inTm.tm_mon = 5;
    inTm.tm_mday = 15;
    inTm.tm_hour = 12;
    inTm.tm_isdst = 1;
    tDst = mktime64(&inTm);
    ICUNIT_GOTO_NOT_EQUAL(tDst, (time64_t)-1, (INT64)tDst, EXIT);
    ICUNIT_GOTO_EQUAL((INT64)(t0 - tDst), (INT64)DST045_FORWARD_SEC, (INT64)(t0 - tDst),
                      EXIT);

    /* 关闭 DST 后 gmtoff 回落(时区无关的相对断言) */
    ret = dst_disable();
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    tmRet = localtime64_r(&t0, &dstOff);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)tmRet, (UINTPTR)NULL, (UINTPTR)tmRet, EXIT);
    ICUNIT_GOTO_EQUAL(dstOff.tm_isdst, 0, dstOff.tm_isdst, EXIT);
    ICUNIT_GOTO_EQUAL((INT64)(dstOn.__tm_gmtoff - dstOff.__tm_gmtoff),
                      (INT64)DST045_FORWARD_SEC, (INT64)(dstOn.__tm_gmtoff - dstOff.__tm_gmtoff),
                      EXIT);

EXIT:
    (VOID)dst_disable();
    return LOS_OK;
}

VOID ItPosixTime045(VOID)
{
    TEST_ADD_CASE("ItPosixTime045", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
