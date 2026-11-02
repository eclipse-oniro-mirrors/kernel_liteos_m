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
 * 用例名称: ItPosixTime047
 * 用例描述: 夏令时配置边界与查询容错矩阵(035/039 补全)
 *
 * 覆盖目标:
 *   lib/posix/src/tzdst.c:
 *     CheckTimeValid     :274      日字段非数字("Feb-3x") → -1
 *     GetMonthDayFromString :299   日超当月长度("Feb-30", 28/29 之外) → -1
 *     DstGetDayOfMonth   :380      1970 前时间(days<0)星期推算(负时间边界)
 *     DstGetDayOfMonth   :396      第 5 周回退("Nov-5th-Fri" → 最后一个周五)
 *     CheckDstPeriodInner :537     mktime 输入 isdst==0 → FALSE(不采信 DST)
 *     dst_inquire        :659-660  start/end==NULL → -1
 *     (dst_inquire INT_MAX: 实测 int 年份上限换算 ≈6.77e16s < __secs_to_tm
 *      阈值 6.79e16s, 解码不失败——矩阵原文 "-1" 按现实现改为边界不溢出断言)
 *
 * 断言口径(DST 周中格式: 起 "Mar-1st-Sun 02:00:00" 止 "Nov-5th-Fri 02:59:59"):
 *   - dst_enable("Feb-3x ...") == -1 / dst_enable("Feb-30 ...") == -1
 *   - 合法 enable 后 dst_inquire(126) 起=3-1/止=11-27(第 5 周五不存在,
 *     回退到最后一个周五 27 日; 初版误写 28——2026-11-28 为周六)
 *   - dst_inquire(126, NULL, &e) == -1
 *   - dst_inquire(INT_MAX, &s, &e) == 0 且 s.tm_year==INT_MAX(int 年份上界不溢出)
 *   - 期内 mktime(isdst=0) == mktime(isdst=1) + 3600(isdst=0 不采信 DST)
 *   - DST 开启下 localtime_r((time_t)-100000) 不崩溃且年份==1969
 *     (1969 负时间触发 DstGetDayOfMonth :380 负天数星期推算)
 */

/* 用例简要描述: 夏令时配置边界与查询容错 */
#include "It_posix_time.h"
#include <limits.h>
#include "tzdst.h"

#define DST047_YEAR_TM 126 /* 2026 */

static UINT32 Testcase(VOID)
{
    struct tm startTm = {0};
    struct tm endTm = {0};
    struct tm inTm = {0};
    struct tm back = {0};
    struct tm *tmRet = NULL;
    time_t t0;
    time_t tDst;
    time_t neg = (time_t)-100000;
    INT32 ret;

    /* 前置: DST 关闭态 */
    ret = dst_inquire(DST047_YEAR_TM, &startTm, &endTm);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* 1. 日字段非数字: CheckTimeValid :274 → -1 */
    ret = dst_enable("Feb-3x 03:00:00", "Mar-01 03:00:00", 3600);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* 2. 日超当月长度("Feb-30": 非 29 特判、≤31 但 >28): :299 → -1 */
    ret = dst_enable("Feb-30 03:00:00", "Mar-01 03:00:00", 3600);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* 3. 合法周中格式(起 3 月第 1 个周日, 止 11 月第 5 个周五) */
    ret = dst_enable("Mar-1st-Sun 02:00:00", "Nov-5th-Fri 02:59:59", 3600);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 4. dst_inquire 正常回读: 起 3-1(2026-03-01 恰为周日), 止 11-27(第 5 周五回退) */
    (VOID)memset_s(&startTm, sizeof(startTm), 0, sizeof(startTm));
    (VOID)memset_s(&endTm, sizeof(endTm), 0, sizeof(endTm));
    ret = dst_inquire(DST047_YEAR_TM, &startTm, &endTm);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(startTm.tm_mon, 2, startTm.tm_mon, EXIT);   /* 2: March */
    ICUNIT_GOTO_EQUAL(startTm.tm_mday, 1, startTm.tm_mday, EXIT); /* 2026-03-01 Sunday */
    ICUNIT_GOTO_EQUAL(endTm.tm_mon, 10, endTm.tm_mon, EXIT);      /* 10: November */
    /* :396 回退到最后一个周五: 2026-11-01 为周日, 周五为 6/13/20/27,
     * 不存在第 5 个周五 → 回退到 27(初版误写 28, 28 为周六, 实测 27) */
    ICUNIT_GOTO_EQUAL(endTm.tm_mday, 27, endTm.tm_mday, EXIT);

    /* 5. dst_inquire NULL 出参: :659-660 → -1 */
    ret = dst_inquire(DST047_YEAR_TM, NULL, &endTm);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* 6. int 年份上界: 解码/换算不溢出(边界不崩溃), 回读 tm_year==INT_MAX */
    (VOID)memset_s(&startTm, sizeof(startTm), 0, sizeof(startTm));
    (VOID)memset_s(&endTm, sizeof(endTm), 0, sizeof(endTm));
    ret = dst_inquire(INT_MAX, &startTm, &endTm);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(startTm.tm_year, INT_MAX, startTm.tm_year, EXIT);

    /* 7. 期内 mktime isdst=0 不采信 DST(:537): t0 == t(isdst=1) + 3600 */
    inTm.tm_year = DST047_YEAR_TM;
    inTm.tm_mon = 5;    /* 5: June, 期内 */
    inTm.tm_mday = 15;
    inTm.tm_hour = 12;
    inTm.tm_isdst = 0;
    t0 = mktime(&inTm);
    ICUNIT_GOTO_NOT_EQUAL(t0, (time_t)-1, t0, EXIT);
    inTm.tm_isdst = 1;
    tDst = mktime(&inTm);
    ICUNIT_GOTO_NOT_EQUAL(tDst, (time_t)-1, tDst, EXIT);
    ICUNIT_GOTO_EQUAL(t0 - tDst, 3600, t0 - tDst, EXIT);

    /* 8. DST 开启下负时间(1969): DstGetDayOfMonth :380 负天数推算, 不崩溃 */
    (VOID)memset_s(&back, sizeof(back), 0, sizeof(back));
    tmRet = localtime_r(&neg, &back);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)tmRet, (UINTPTR)NULL, (UINTPTR)tmRet, EXIT);
    ICUNIT_GOTO_EQUAL(back.tm_year, 69, back.tm_year, EXIT); /* 69: 1969 */

EXIT:
    (VOID)dst_disable();
    return LOS_OK;
}

VOID ItPosixTime047(VOID)
{
    TEST_ADD_CASE("ItPosixTime047", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
