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
 * 用例名称: ItPosixTime043
 * 用例描述: RTC 各 hook 成员路径与失败回退(037/038 未覆盖成员补全)
 *
 * 覆盖目标:
 *   lib/posix/src/time.c:
 *     clock()        :485      RtcGetTickHook 非空 → 返回 hook 值
 *     GetCurrentTime :504-506  tick hook 差值计算(g_systickBase 基准)
 *     OsEffectiveTimezone :634-636  RtcGetTimezoneHook 优先于全局 timezone
 *     gettimeofday   :880-881  RtcGetTimeHook 成功 → tv 直取 hook 微秒值
 *     gettimeofday   :894-896  RtcGetTimezoneHook → tz_minuteswest
 *     settimeofday   :952      RtcSetTimeHook 返回负 → -1
 *     settimeofday   :964      RtcGetTickHook 记录 g_systickBase
 *
 * 断言口径(固定桩值):
 *   - 注册 {tick=T0, time=ok, set=-1, tz=-28800}:
 *     clock()==T0; gettimeofday tv_sec==RTC_SEC/tv_usec==0;
 *     gettimeofday tz_minuteswest==-480; localtime_r(2020-07-01 UTC) 小时==8
 *     (hook 时区 UTC+8); settimeofday → -1(set hook 失败)
 *   - 重注册 {tick=T0, time=err, set=ok} 后 settimeofday 成功:
 *     time() 回退 GetCurrentTime: tick==systickBase → 差值 0 →
 *     返回 g_rtcTimeBase/1000 == 所设秒值
 *   - EXIT: 注册全 NULL 成员结构体注销 + 恢复原全局时区
 */

/* 用例简要描述: RTC 各 hook 成员路径与回退 */
#define _GNU_SOURCE /* musl: settimeofday 原型需要 _GNU_SOURCE 可见 */
#include "It_posix_time.h"
#include "rtc_time_hook.h"

#define RTC043_TICK     0x100000        /* 固定 tick 桩值 */
#define RTC043_SEC      1593561600      /* 2020-07-01 00:00:00 UTC */
#define RTC043_TZ_EAST8 (-28800)        /* UTC+8, POSIX 西正取负 */
#define RTC043_SET_SEC  1600000000      /* settimeofday 成功路径设定值 */

static INT32 Rtc043GetTimeOkHook(UINT64 *usec)
{
    *usec = (UINT64)RTC043_SEC * 1000000; /* 1000000: us per sec */
    return 0;
}

static INT32 Rtc043GetTimeErrHook(UINT64 *usec)
{
    (VOID)usec;
    return 1;
}

static INT32 Rtc043SetTimeFailHook(UINT64 msec, UINT64 *usec)
{
    (VOID)msec;
    (VOID)usec;
    return -1;
}

static INT32 Rtc043SetTimeOkHook(UINT64 msec, UINT64 *usec)
{
    (VOID)msec;
    (VOID)usec;
    return 0;
}

static UINT64 Rtc043GetTickHook(VOID)
{
    return RTC043_TICK;
}

static INT32 Rtc043GetTimezoneHook(INT32 *tz)
{
    *tz = RTC043_TZ_EAST8;
    return 0;
}

static UINT32 Testcase(VOID)
{
    struct RtcTimeHook cfgA = {0};
    struct RtcTimeHook cfgB = {0};
    struct RtcTimeHook cfgEmpty = {0};
    struct timeval tv = {0};
    struct timezone tz = {0};
    struct timezone tzSave = {0};
    struct tm localTm = {0};
    time_t t = (time_t)RTC043_SEC;
    time_t timeRet;
    clock_t clk;
    INT32 ret;

    /* 保存原全局时区(settimeofday(NULL,&tz) 路径用于恢复) */
    ret = gettimeofday(NULL, &tzSave);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* --- 段 1: {tick, time-ok, set-fail, tz} --- */
    cfgA.RtcGetTickHook = Rtc043GetTickHook;
    cfgA.RtcGetTimeHook = Rtc043GetTimeOkHook;
    cfgA.RtcSetTimeHook = Rtc043SetTimeFailHook;
    cfgA.RtcGetTimezoneHook = Rtc043GetTimezoneHook;
    LOS_RtcHookRegister(&cfgA);

    /* clock() 走 tick hook: :485 */
    clk = clock();
    ICUNIT_GOTO_EQUAL((UINT64)clk, (UINT64)RTC043_TICK, (UINT64)clk, EXIT);

    /* gettimeofday tv 走 time hook: :880-881 */
    ret = gettimeofday(&tv, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL((INT64)tv.tv_sec, (INT64)RTC043_SEC, (INT64)tv.tv_sec, EXIT);
    ICUNIT_GOTO_EQUAL(tv.tv_usec, 0, tv.tv_usec, EXIT);

    /* gettimeofday tz 走 timezone hook: :894-896 */
    (VOID)memset_s(&tz, sizeof(tz), 0, sizeof(tz));
    ret = gettimeofday(NULL, &tz);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(tz.tz_minuteswest, RTC043_TZ_EAST8 / 60, tz.tz_minuteswest, EXIT);

    /* localtime_r 走 OsEffectiveTimezone 的 hook 分支: :634-636 */
    (VOID)memset_s(&localTm, sizeof(localTm), 0, sizeof(localTm));
    (VOID)localtime_r(&t, &localTm);
    ICUNIT_GOTO_EQUAL(localTm.tm_hour, 8, localTm.tm_hour, EXIT); /* 8: UTC+8 */
    ICUNIT_GOTO_EQUAL(localTm.tm_mday, 1, localTm.tm_mday, EXIT);

    /* settimeofday 的 set hook 失败: :952 → -1 */
    tv.tv_sec = (time_t)RTC043_SET_SEC;
    tv.tv_usec = 0;
    ret = settimeofday(&tv, NULL);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* --- 段 2: {tick, time-err, set-ok} → set hook 成功 + g_systickBase 记录 --- */
    cfgB.RtcGetTickHook = Rtc043GetTickHook;
    cfgB.RtcGetTimeHook = Rtc043GetTimeErrHook;
    cfgB.RtcSetTimeHook = Rtc043SetTimeOkHook;
    LOS_RtcHookRegister(&cfgB);

    ret = settimeofday(&tv, NULL); /* :964 g_systickBase = tick hook 值 */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* time() 回退 GetCurrentTime: currentTick==systickBase → 差值 0 →
       返回 g_rtcTimeBase/1000 == 所设秒值(:504-506) */
    timeRet = time(NULL);
    ICUNIT_GOTO_EQUAL((INT64)timeRet, (INT64)RTC043_SET_SEC, (INT64)timeRet, EXIT);

EXIT:
    /* 注销 hook(全 NULL 成员覆盖式注销)并恢复原全局时区 */
    LOS_RtcHookRegister(&cfgEmpty);
    (VOID)settimeofday(NULL, &tzSave);
    return LOS_OK;
}

VOID ItPosixTime043(VOID)
{
    TEST_ADD_CASE("ItPosixTime043", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
