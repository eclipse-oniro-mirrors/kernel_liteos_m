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
 * 用例名称: ItPosixMutex088
 * 用例描述: PTHREAD_MUTEX_INITIALIZER 静态锁三种首次加锁懒创建
 *
 * 覆盖目标:
 *   lib/posix/src/pthread_mutex.c:
 *     pthread_mutex_lock     :533-534 后的懒创建(handle==INVALID 时 LOS_MuxCreate, 已覆盖)
 *     pthread_mutex_timedlock :487-489 懒创建分支(未覆盖)
 *     pthread_mutex_trylock   :569-571 懒创建分支(未覆盖)
 *
 * 断言口径:
 *   三个独立的 PTHREAD_MUTEX_INITIALIZER 静态锁分别以
 *   lock / trylock / timedlock 首次加锁:
 *   - 三种首次加锁均返回 0(懒创建成功)
 *   - 对应 unlock 均返回 0, destroy 均返回 0
 */

/* 用例简要描述: 静态初始化互斥锁懒创建路径 */
#include "It_posix_mutex.h"
#include <time.h>

static pthread_mutex_t g_mux088Lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_mux088Try = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_mux088Timed = PTHREAD_MUTEX_INITIALIZER;

static UINT32 Testcase(VOID)
{
    struct timespec ts = {0};
    struct timespec now = {0};
    INT32 ret;

    /* 1. lock 懒创建 */
    ret = pthread_mutex_lock(&g_mux088Lock);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_mutex_unlock(&g_mux088Lock);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 2. trylock 懒创建(:569-571) */
    ret = pthread_mutex_trylock(&g_mux088Try);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_mutex_unlock(&g_mux088Try);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 3. timedlock 懒创建(:487-489), 有效未来超时(+2s) */
    (VOID)clock_gettime(CLOCK_REALTIME, &now);
    ts.tv_sec = now.tv_sec + 2; /* 2: seconds ahead */
    ts.tv_nsec = now.tv_nsec;
    ret = pthread_mutex_timedlock(&g_mux088Timed, &ts);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_mutex_unlock(&g_mux088Timed);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 三个懒创建锁全部回收 */
    ret = pthread_mutex_destroy(&g_mux088Lock);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_mutex_destroy(&g_mux088Try);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_mutex_destroy(&g_mux088Timed);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    return LOS_OK;
EXIT:
    (VOID)pthread_mutex_destroy(&g_mux088Lock);
    (VOID)pthread_mutex_destroy(&g_mux088Try);
    (VOID)pthread_mutex_destroy(&g_mux088Timed);
    return LOS_NOK;
}

VOID ItPosixMux088(VOID)
{
    TEST_ADD_CASE("ItPosixMux088", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
