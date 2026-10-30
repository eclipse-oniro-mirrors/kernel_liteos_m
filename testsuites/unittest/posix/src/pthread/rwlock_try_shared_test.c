/*
 * Copyright (c) 2022-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <pthread.h>
#include "ohos_types.h"
#include "posix_test.h"
#include "los_config.h"
#include "kernel_test.h"
#include "log.h"

/* *
 * @tc.desc      : register a test suite, this suite is used to test basic flow and interface dependency
 * @param        : subsystem name is posix
 * @param        : module name is pthread
 * @param        : test suit name is PosixPthreadRwlockTrySharedTest
 */
LITE_TEST_SUIT(Posix, Pthread, PosixPthreadRwlockTrySharedTest);

/* *
 * @tc.setup     : setup for all testcases
 * @return       : setup result, TRUE is success, FALSE is fail
 */
static BOOL PosixPthreadRwlockTrySharedTestSetUp(void)
{
    return TRUE;
}

/* *
 * @tc.teardown  : teardown for all testcases
 * @return       : teardown result, TRUE is success, FALSE is fail
 */
static BOOL PosixPthreadRwlockTrySharedTestTearDown(void)
{
    LOG("+-------------------------------------------+\n");
    return TRUE;
}

/* *
 * @tc.number    : TEST_RWLOCK_TRY_SHARED_001
 * @tc.name      : tryrdlock acquires the read lock; owner re-acquire returns EDEADLK
 * @tc.desc      : [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixPthreadRwlockTrySharedTest, testRwlockTryShared001, Function | MediumTest | Level1)
{
    pthread_rwlock_t rwl;
    int ret;

    ret = pthread_rwlock_init(&rwl, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_tryrdlock(&rwl);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_tryrdlock(&rwl);
    ICUNIT_ASSERT_EQUAL(ret, EDEADLK, ret);

    ret = pthread_rwlock_unlock(&rwl);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_destroy(&rwl);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return 0;
}

/* *
 * @tc.number    : TEST_RWLOCK_TRY_SHARED_002
 * @tc.name      : tryrdlock returns EINVAL for NULL or uninitialized rwlock
 * @tc.desc      : [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixPthreadRwlockTrySharedTest, testRwlockTryShared002, Function | MediumTest | Level1)
{
    pthread_rwlock_t rwl = {0};
    int ret;

    ret = pthread_rwlock_tryrdlock(NULL);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    ret = pthread_rwlock_tryrdlock(&rwl);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    return 0;
}

/* *
 * @tc.number    : TEST_RWLOCK_TRY_SHARED_003
 * @tc.name      : tryrdlock returns EDEADLK when the calling thread holds the write lock
 * @tc.desc      : [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixPthreadRwlockTrySharedTest, testRwlockTryShared003, Function | MediumTest | Level1)
{
    pthread_rwlock_t rwl;
    int ret;

    ret = pthread_rwlock_init(&rwl, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_wrlock(&rwl);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_tryrdlock(&rwl);
    ICUNIT_ASSERT_EQUAL(ret, EDEADLK, ret);

    ret = pthread_rwlock_unlock(&rwl);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_destroy(&rwl);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return 0;
}

static pthread_rwlock_t g_rwlockTryShared;
static volatile int g_rwlockWriterState;

static void *RwlockTrySharedWriter(void *arg)
{
    int ret;

    (void)arg;
    ret = pthread_rwlock_wrlock(&g_rwlockTryShared);
    if (ret != 0) {
        return (void *)(uintptr_t)ret;
    }

    g_rwlockWriterState = 1;
    while (g_rwlockWriterState == 1) {
        LOS_TaskDelay(1);
    }

    ret = pthread_rwlock_unlock(&g_rwlockTryShared);
    if (ret != 0) {
        return (void *)(uintptr_t)ret;
    }

    return NULL;
}

/* *
 * @tc.number    : TEST_RWLOCK_TRY_SHARED_004
 * @tc.name      : tryrdlock returns EBUSY while another thread holds the write lock
 * @tc.desc      : [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixPthreadRwlockTrySharedTest, testRwlockTryShared004, Function | MediumTest | Level1)
{
    pthread_t tid;
    int ret;

    g_rwlockWriterState = 0;
    ret = pthread_rwlock_init(&g_rwlockTryShared, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_create(&tid, NULL, RwlockTrySharedWriter, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    while (g_rwlockWriterState == 0) {
        LOS_TaskDelay(1);
    }

    ret = pthread_rwlock_tryrdlock(&g_rwlockTryShared);
    ICUNIT_ASSERT_EQUAL(ret, EBUSY, ret);

    g_rwlockWriterState = 0;
    ret = pthread_join(tid, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_tryrdlock(&g_rwlockTryShared);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_unlock(&g_rwlockTryShared);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_destroy(&g_rwlockTryShared);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return 0;
}

void PosixPthreadRwlockTrySharedFuncTest(void)
{
    LOG("begin PosixPthreadRwlockTrySharedFuncTest....");
    RUN_ONE_TESTCASE(testRwlockTryShared001);
    RUN_ONE_TESTCASE(testRwlockTryShared002);
    RUN_ONE_TESTCASE(testRwlockTryShared003);
    RUN_ONE_TESTCASE(testRwlockTryShared004);
    return;
}
