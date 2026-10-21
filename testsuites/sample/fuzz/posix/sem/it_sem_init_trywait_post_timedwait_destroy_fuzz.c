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

/* ItFuzzPosixSem: sem_init/trywait/post/timedwait/destroy */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_posix_sem_fuzz.h"

static void *FzPosixThreadEntry(void *arg) __attribute__((unused));
static void *FzPosixThreadEntry(void *arg) { (VOID)arg; return NULL; }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    sem_t sem; INT32 ret; struct timespec ts;
    for (int i = 0; i < CYCLE_SLEEP_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        ret = sem_init(&sem, 0, (*(INT32 *)DT_SetGetS32(&g_element[0], 1)) & 1);
        if (ret == 0) {
            (VOID)sem_trywait(&sem); (VOID)sem_post(&sem);
            (VOID)clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 2000000;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }
            (VOID)sem_timedwait(&sem, &ts);
            (VOID)sem_destroy(&sem);
        }
    }

    /* directed: sem_wait/getvalue + named semaphores */
    {
        sem_t s2;
        int v = 0;
        if (sem_init(&s2, 0, 1) == 0) {
            (VOID)sem_wait(&s2);
            (VOID)sem_getvalue(&s2, &v);
            (VOID)sem_destroy(&s2);
        }
        {
            sem_t *so = sem_open("/fzso", O_CREAT, 0644, 1);
            if (so != SEM_FAILED) {
                (VOID)sem_close(so);
                (VOID)sem_unlink("/fzso");
            }
        }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzPosixSem(VOID)
{
    TEST_ADD_CASE("ItFuzzPosixSem", TestCase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
