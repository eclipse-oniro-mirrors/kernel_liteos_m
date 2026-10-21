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

/* ItFuzzPosixRwlock: pthread_rwlock_rd/wr/try/unlock */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_posix_rwlock_fuzz.h"

static void *FzPosixThreadEntry(void *arg) __attribute__((unused));
static void *FzPosixThreadEntry(void *arg) { (VOID)arg; return NULL; }

static pthread_rwlock_t *g_fzRw;
static volatile UINT32 g_fzRwHeld;
static void *FzRwReaderEntry(void *arg)
{
    (VOID)arg;
    if (pthread_rwlock_rdlock(g_fzRw) == 0) {
        g_fzRwHeld++;
        LOS_TaskDelay(6);
        (VOID)pthread_rwlock_unlock(g_fzRw);
    }
    return NULL;
}

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        if (pthread_rwlock_rdlock(&rw) == 0) { (VOID)pthread_rwlock_unlock(&rw); }
        if (pthread_rwlock_tryrdlock(&rw) == 0) { (VOID)pthread_rwlock_unlock(&rw); }
        if (pthread_rwlock_wrlock(&rw) == 0) { (VOID)pthread_rwlock_unlock(&rw); }
    }
    (VOID)pthread_rwlock_destroy(&rw);
    /* directed: writer blocked by readers (node add/remove paths) */
    {
        pthread_rwlock_t rw3;
        if (pthread_rwlock_init(&rw3, NULL) == 0) {
            pthread_t t1, t2;
            UINT32 w = 0;
            g_fzRw = &rw3; g_fzRwHeld = 0;
            if (pthread_create(&t1, NULL, FzRwReaderEntry, NULL) == 0 &&
                pthread_create(&t2, NULL, FzRwReaderEntry, NULL) == 0) {
                while (g_fzRwHeld < 2 && w < 100) { LOS_TaskDelay(1); w++; }
                (VOID)pthread_rwlock_wrlock(&rw3);   /* blocked until readers release */
                (VOID)pthread_rwlock_unlock(&rw3);
                (VOID)pthread_join(t1, NULL);
                (VOID)pthread_join(t2, NULL);
            }
            (VOID)pthread_rwlock_destroy(&rw3);
        }
    }

    /* directed: rwlockattr family + init/trywrlock flavors */
    {
        pthread_rwlockattr_t ra;
        int ps = 0;
        pthread_rwlock_t rw2;
        if (pthread_rwlockattr_init(&ra) == 0) {
            (VOID)pthread_rwlockattr_setpshared(&ra, PTHREAD_PROCESS_PRIVATE);
            (VOID)pthread_rwlockattr_getpshared(&ra, &ps);
            if (pthread_rwlock_init(&rw2, &ra) == 0) {
                (VOID)pthread_rwlock_rdlock(&rw2);
                (VOID)pthread_rwlock_tryrdlock(&rw2);
                (VOID)pthread_rwlock_trywrlock(&rw2);
                (VOID)pthread_rwlock_unlock(&rw2);
                (VOID)pthread_rwlock_unlock(&rw2);
                (VOID)pthread_rwlock_destroy(&rw2);
            }
            (VOID)pthread_rwlockattr_destroy(&ra);
        }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzPosixRwlock(VOID)
{
    TEST_ADD_CASE("ItFuzzPosixRwlock", TestCase, TEST_LIB, TEST_PTHREAD, TEST_LEVEL0, TEST_FUNCTION);
}
