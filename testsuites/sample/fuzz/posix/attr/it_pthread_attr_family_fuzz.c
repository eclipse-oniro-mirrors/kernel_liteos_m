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

/* ItFuzzPosixAttr: pthread_attr_* family */
/* DTFuzz: mutate target API params, pre-create resource, cleanup */

#include "it_posix_attr_fuzz.h"

static void *FzPosixThreadEntry(void *arg) __attribute__((unused));
static void *FzPosixThreadEntry(void *arg) { (VOID)arg; return NULL; }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();

    pthread_attr_t attr; struct sched_param sp = {0};
    for (int i = 0; i < CYCLE_TOTAL_TIMES; i++) {
        FuzzWatchdogFeed(); FuzzHeartbeat(i);
        (VOID)pthread_attr_init(&attr);
        (VOID)pthread_attr_setdetachstate(&attr, (*(INT32 *)DT_SetGetS32(&g_element[0], 0)) % 3);
        sp.sched_priority = (*(INT32 *)DT_SetGetS32(&g_element[1], 0)) % 40;
        (VOID)pthread_attr_setschedparam(&attr, &sp);
        (VOID)pthread_attr_setschedpolicy(&attr, (*(INT32 *)DT_SetGetS32(&g_element[2], 0)) % 3);
        (VOID)pthread_attr_getschedparam(&attr, (struct sched_param *)g_element[3].buf);
        (VOID)pthread_attr_destroy(&attr);
    }

    /* directed: attr scope/inheritsched/policy/stack/affinity family */
    {
        pthread_attr_t a2;
        int v = 0;
        void *p = NULL;
        size_t sz = 0;
        static char stackbuf[4096];    /* static: stack outlives the attr use */
        cpu_set_t cs;
        if (pthread_attr_init(&a2) == 0) {
            (VOID)pthread_attr_setscope(&a2, PTHREAD_SCOPE_SYSTEM);
            (VOID)pthread_attr_getscope(&a2, &v);
            (VOID)pthread_attr_setinheritsched(&a2, PTHREAD_EXPLICIT_SCHED);
            (VOID)pthread_attr_getinheritsched(&a2, &v);
            (VOID)pthread_attr_setschedpolicy(&a2, SCHED_RR);
            (VOID)pthread_attr_getschedpolicy(&a2, &v);
            (VOID)pthread_attr_setstacksize(&a2, 4096);
            (VOID)pthread_attr_getstacksize(&a2, &sz);
            (VOID)pthread_attr_setstack(&a2, stackbuf, sizeof(stackbuf));
            (VOID)pthread_attr_getstack(&a2, &p, &sz);
            CPU_ZERO(&cs); CPU_SET(0, &cs);
            (VOID)pthread_attr_setaffinity_np(&a2, sizeof(cs), &cs);
            (VOID)pthread_attr_getaffinity_np(&a2, sizeof(cs), &cs);
            (VOID)pthread_attr_getdetachstate(&a2, &v);
            (VOID)pthread_attr_destroy(&a2);
        }
    }

    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}

VOID ItFuzzPosixAttr(VOID)
{
    TEST_ADD_CASE("ItFuzzPosixAttr", TestCase, TEST_LIB, TEST_PTHREAD, TEST_LEVEL0, TEST_FUNCTION);
}
