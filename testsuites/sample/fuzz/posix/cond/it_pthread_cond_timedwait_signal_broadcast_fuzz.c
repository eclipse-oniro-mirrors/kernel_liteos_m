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

/* ItFuzzPosixCond: pthread_cond_timedwait/signal/broadcast */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_posix_cond_fuzz.h"

static void *FzPosixThreadEntry(void *arg) __attribute__((unused));
static void *FzPosixThreadEntry(void *arg) { (VOID)arg; return NULL; }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t c = PTHREAD_COND_INITIALIZER; struct timespec ts;
    for (int i = 0; i < CYCLE_SLEEP_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        (VOID)pthread_mutex_lock(&m);
        (VOID)clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 2000000;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }
        (VOID)pthread_cond_timedwait(&c, &m, &ts);
        (VOID)pthread_mutex_unlock(&m);
        (VOID)pthread_cond_signal(&c); (VOID)pthread_cond_broadcast(&c);
    }
    (VOID)pthread_cond_destroy(&c); (VOID)pthread_mutex_destroy(&m);

    /* directed: condattr family */
    {
        pthread_condattr_t ca;
        clockid_t ck = CLOCK_REALTIME;
        int ps = 0;
        if (pthread_condattr_init(&ca) == 0) {
            (VOID)pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
            (VOID)pthread_condattr_getclock(&ca, &ck);
            (VOID)pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_PRIVATE);
            (VOID)pthread_condattr_getpshared(&ca, &ps);
            (VOID)pthread_condattr_destroy(&ca);
        }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzPosixCond(VOID)
{
    TEST_ADD_CASE("ItFuzzPosixCond", TestCase, TEST_LIB, TEST_PTHREAD, TEST_LEVEL0, TEST_FUNCTION);
}
