/*
 * Copyright (c) 2022 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_posix_pthread.h"
#ifdef LOSCFG_KERNEL_SMP
#include <sched.h>
extern int sched_getaffinity(pid_t, size_t, cpu_set_t *);
extern int sched_setaffinity(pid_t, size_t, const cpu_set_t *);
#endif

static VOID *PthreadF01(VOID *argument)
{
    sleep(1);
    return NULL;
}

static UINT32 Testcase(VOID)
{
    pthread_t newTh1, newTh2;
    INT32 ret;

    ret = pthread_create(&newTh1, NULL, PthreadF01, NULL);
#ifdef LOSCFG_KERNEL_SMP
    {
        cpu_set_t inheritCpuset;
        (void)sched_getaffinity(0, sizeof(cpu_set_t), &inheritCpuset);
        (void)sched_setaffinity((pid_t)(UINTPTR)newTh1, sizeof(cpu_set_t), &inheritCpuset);
    }
#endif
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_create(&newTh2, NULL, PthreadF01, NULL);
#ifdef LOSCFG_KERNEL_SMP
    {
        cpu_set_t inheritCpuset;
        (void)sched_getaffinity(0, sizeof(cpu_set_t), &inheritCpuset);
        (void)sched_setaffinity((pid_t)(UINTPTR)newTh2, sizeof(cpu_set_t), &inheritCpuset);
    }
#endif
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_equal(newTh1, newTh2);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_join(newTh1, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_join(newTh2, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    return LOS_OK;
}

/**
 * @tc.name: ItPosixPthread020
 * @tc.desc: Test interface pthread_equal
 * @tc.type: FUNC
 * @tc.require: issueI5TIRQ
 */

VOID ItPosixPthread020(VOID)
{
    TEST_ADD_CASE("ItPosixPthread020", Testcase, TEST_LIB, TEST_FN_COMPAT_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}
