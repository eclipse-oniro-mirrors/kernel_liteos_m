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

/* ItFuzzPthreadSelf: pthread_self/equal */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_posix_pthread_fuzz.h"

static void *FzPosixThreadEntry(void *arg) __attribute__((unused));
static void *FzPosixThreadEntry(void *arg) { (VOID)arg; return NULL; }


static VOID __attribute__((unused)) FzOnceFn(VOID) {}
static VOID __attribute__((unused)) FzCleanupFn(VOID *arg) { (VOID)arg; }
static VOID __attribute__((unused)) FzKeyDtor(void *v) { (VOID)v; }
static pthread_key_t *g_fzKey;
static void *FzCancelTargetEntry(void *arg)
{
    (VOID)arg;
    pthread_cleanup_push(FzCleanupFn, NULL);
    (VOID)pthread_setspecific(*g_fzKey, (void *)1);
    for (volatile int i = 0; i < 2000000; i++) {}   /* finite busy window, cancellable */
    pthread_cleanup_pop(0);
    return NULL;
}
static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    pthread_t me = pthread_self();
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        (VOID)pthread_equal(me, me);
    }

    /* directed: pthread auxiliary APIs */
    {
        static pthread_once_t g_once = PTHREAD_ONCE_INIT;
        struct sched_param sp = {0};
        int pol = 0;
        char nm[16] = {0};
        int i = 0;
        pthread_key_t k;

        (VOID)pthread_once(&g_once, FzOnceFn);
        sp.sched_priority = 24;
        (VOID)pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
        (VOID)pthread_getschedparam(pthread_self(), &pol, &sp);
        (VOID)pthread_setschedprio(pthread_self(), 24);
        (VOID)pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
        (VOID)pthread_setname_np(pthread_self(), "fz");
        (VOID)pthread_getname_np(pthread_self(), nm, sizeof(nm));
        if (pthread_key_create(&k, NULL) == 0) {
            (VOID)pthread_setspecific(k, &i);
            (VOID)pthread_getspecific(k);
            (VOID)pthread_key_delete(k);
        }
        /* pthread_cleanup_push/pop moved into the child thread only: calling them
         * from a non-pthread task dereferences TCB->arg (defect, question 39) */
        {
            static pthread_key_t g_k;
            pthread_t tc;
            if (pthread_key_create(&g_k, FzKeyDtor) == 0) {
                g_fzKey = &g_k;
                if (pthread_create(&tc, NULL, FzCancelTargetEntry, NULL) == 0) {
                    LOS_TaskDelay(2);              /* let target register key/cleanup */
                    (VOID)pthread_cancel(tc);      /* DoPthreadCancel + exit paths */
                    (VOID)pthread_join(tc, NULL);
                }
                (VOID)pthread_key_delete(g_k);
            }
        }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzPthreadSelf(VOID)
{
    TEST_ADD_CASE("ItFuzzPthreadSelf", TestCase, TEST_LIB, TEST_PTHREAD, TEST_LEVEL0, TEST_FUNCTION);
}
