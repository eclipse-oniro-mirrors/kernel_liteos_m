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
 * 用例名称: ItPosixPthread040
 * 用例描述: 线程 key 上限/失效 EAGAIN/删除析构/取消类型回写
 *
 * 覆盖目标:
 *   lib/posix/src/pthread.c:
 *     pthread_key_create   :703-704  key 数达 PTHREAD_KEYS_MAX(128) → EAGAIN
 *     pthread_setspecific  :792-794  key 未用(已删) → EAGAIN
 *     pthread_key_delete   :749-752  删 key 时对在用值调用 dtor 并清零
 *     pthread_setcanceltype :402     *oldType 回写原类型
 *
 * 断言口径(key 族必须在 pthread 上下文调用, 用子线程执行):
 *   - 连续 key_create 前 128 个成功, 第 129 个返回 EAGAIN
 *   - key_delete 后 setspecific(该 key) 返回 EAGAIN
 *   - key_delete 时 dtor 恰收到在线设置的值
 *   - setcanceltype 两次回写: 默认 DEFERRED → ASYNC 后回写 DEFERRED, 再回写 ASYNC
 */

/* 用例简要描述: 线程 key 上限析构与回写 */
#include "It_posix_pthread.h"

#define PTHREAD040_KEY_VALUE ((void *)0x4040)

static volatile INT32 g_pthread040DtorCount = 0;
static volatile VOID *g_pthread040DtorValue = NULL;

static VOID Pthread040Dtor(VOID *value)
{
    g_pthread040DtorCount++;
    g_pthread040DtorValue = value;
}

static VOID *Pthread040KeyThread(VOID *arg)
{
    pthread_key_t keys[PTHREAD_KEYS_MAX];
    pthread_key_t keyNew;
    VOID *valueGot = NULL;
    INT32 oldType = -1;
    INT32 oldType2 = -1;
    INT32 ret;
    INT32 i;
    INT32 okCount = 0;
    INT32 eagainSeen = 0;

    (VOID)arg;
    for (i = 0; i < PTHREAD_KEYS_MAX; i++) {
        keys[i] = (pthread_key_t)-1;
    }

    /* 1. setcanceltype 回写: 默认类型应为 DEFERRED */
    ret = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldType);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_THREAD);
    ICUNIT_GOTO_EQUAL(oldType, PTHREAD_CANCEL_DEFERRED, oldType, EXIT_THREAD);
    ret = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &oldType2);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_THREAD);
    ICUNIT_GOTO_EQUAL(oldType2, PTHREAD_CANCEL_ASYNCHRONOUS, oldType2, EXIT_THREAD);

    /* 2. key 上限: 前 128 个成功, 第 129 个 EAGAIN */
    for (i = 0; i <= PTHREAD_KEYS_MAX; i++) { /* 129 次尝试 */
        if (i < PTHREAD_KEYS_MAX) {
            ret = pthread_key_create(&keys[i], NULL);
            if (ret != 0) {
                break;
            }
            okCount++;
        } else {
            ret = pthread_key_create(&keyNew, NULL);
            if (ret == EAGAIN) {
                eagainSeen = 1;
            }
            break;
        }
    }
    ICUNIT_GOTO_EQUAL(okCount, PTHREAD_KEYS_MAX, okCount, EXIT_THREAD);
    ICUNIT_GOTO_EQUAL(eagainSeen, 1, eagainSeen, EXIT_THREAD);

    /* 3. 删除 key[0] 后 setspecific → EAGAIN(:792-794) */
    pthread_key_t keyDeleted = keys[0];
    ret = pthread_key_delete(keyDeleted);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_THREAD);
    keys[0] = (pthread_key_t)-1;
    ret = pthread_setspecific(keyDeleted, PTHREAD040_KEY_VALUE);
    ICUNIT_GOTO_EQUAL(ret, EAGAIN, ret, EXIT_THREAD);

    /* 4. 重建 key(带 dtor) → setspecific → getspecific → delete 时 dtor 收到在用值 */
    ret = pthread_key_create(&keyNew, Pthread040Dtor);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_THREAD);
    ret = pthread_setspecific(keyNew, PTHREAD040_KEY_VALUE);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_THREAD);
    valueGot = pthread_getspecific(keyNew);
    ICUNIT_GOTO_EQUAL((UINTPTR)valueGot, (UINTPTR)PTHREAD040_KEY_VALUE, (UINTPTR)valueGot, EXIT_THREAD);
    ret = pthread_key_delete(keyNew); /* 触发 dtor(在用值) */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_THREAD);
    ICUNIT_GOTO_EQUAL(g_pthread040DtorCount, 1, g_pthread040DtorCount, EXIT_THREAD);
    ICUNIT_GOTO_EQUAL((UINTPTR)g_pthread040DtorValue, (UINTPTR)PTHREAD040_KEY_VALUE,
                      (UINTPTR)g_pthread040DtorValue, EXIT_THREAD);

EXIT_THREAD:
    /* 回收全部已建 key */
    for (i = 0; i < PTHREAD_KEYS_MAX; i++) {
        if (keys[i] != (pthread_key_t)-1) {
            (VOID)pthread_key_delete(keys[i]);
            keys[i] = (pthread_key_t)-1;
        }
    }
    return NULL;
}

static UINT32 Testcase(VOID)
{
    pthread_t thread;
    INT32 ret;

    g_pthread040DtorCount = 0;
    g_pthread040DtorValue = NULL;

    ret = pthread_create(&thread, NULL, Pthread040KeyThread, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_join(thread, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    return LOS_OK;
}

VOID ItPosixPthread040(VOID)
{
    TEST_ADD_CASE("ItPosixPthread040", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}
