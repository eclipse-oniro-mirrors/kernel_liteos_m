/*
 * Copyright (c) 2022-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ItFuzzPosixClock: clock_gettime/settime/getres */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_posix_time_fuzz.h"
#include <sys/time.h>
#include <sys/times.h>

static void *FzPosixThreadEntry(void *arg) __attribute__((unused));
static void *FzPosixThreadEntry(void *arg) { (VOID)arg; return NULL; }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    struct timespec ts; struct timespec res;
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        (VOID)clock_gettime(CLOCK_REALTIME, &ts); (VOID)clock_gettime(CLOCK_MONOTONIC, &ts);
        (VOID)clock_getres(CLOCK_REALTIME, &res);
        ts.tv_sec += (*(INT32 *)DT_SetGetS32(&g_element[0], 0)) & 0xF;
        ts.tv_nsec = (*(INT32 *)DT_SetGetS32(&g_element[1], 0)) & 0xFFFFF;
        (VOID)clock_settime(CLOCK_REALTIME, &ts);
    }

    /* directed: gettimeofday/settimeofday/time/gmtime/localtime/mktime/sleep/times */
    {
        struct timeval tv = {0};
        struct timezone tz = {0};
        struct tm tmv = {0};
        struct tms tbuf;
        time_t t = time(NULL);
        (VOID)gettimeofday(&tv, &tz);
        (VOID)settimeofday(&tv, NULL);
        (VOID)gmtime_r(&t, &tmv);
        (VOID)localtime_r(&t, &tmv);
        (VOID)mktime(&tmv);
        (VOID)times(&tbuf);
        (VOID)sleep(0);
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzPosixClock(VOID)
{
    TEST_ADD_CASE("ItFuzzPosixClock", TestCase, TEST_LIB, TEST_TIME, TEST_LEVEL0, TEST_FUNCTION);
}
