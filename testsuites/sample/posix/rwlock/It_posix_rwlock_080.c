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
 * 用例名称: ItPosixRwlock080
 * 用例描述: 多读者并发与写锁争用语义
 *
 * 覆盖目标:
 *   lib/posix/src/pthread_rwlock.c:
 *     pthread_rwlock_unlock :446-448  多读者时 unlock 单读者(readers>0)直接返回
 *       (ws63 未命中分支, 双读者场景补强; qemu 已覆盖同路径)
 *     pthread_rwlock_trywrlock        readers>0 → EBUSY
 *
 * 断言口径:
 *   两个子任务各持一把读锁(双读者), 主任务(不持锁)作探测方:
 *   - trywrlock → EBUSY(两读者均在持)
 *   - 读者 A unlock(读者 2→1, :446-448 早返回路径)后 trywrlock 仍 EBUSY
 *     (读者 B 仍持锁)
 *   - 读者 B 也 unlock 后(无持有者) trywrlock → 0 再 unlock → 0
 *
 * 实现语义偏离说明(对比初版):
 *   初版由主任务自己持读锁后调 trywrlock。本实现的 trywrlock 经
 *   OsRwlockIsOwner(:284-287)检查调用者是否持有"任一"读/写锁, 持读锁的
 *   调用者请求写锁会命中防自死锁路径返回 EDEADLK(实测 0x23)而非 EBUSY
 *   (读锁未释放时请求写锁必然自死锁, 该防御语义合理)。故双读者改为两个
 *   子任务持有, 主任务作为无锁探测方验证 EBUSY, 多读者/unlock 早返回
 *   (:446-448)覆盖目标不变。
 */

/* 用例简要描述: 多读者并发与写锁争用语义 */
#include "It_posix_rwlock.h"

static pthread_rwlock_t g_rwl080;
static volatile INT32 g_rwlock080Held[2] = {0, 0};     /* 读者已持锁标记 */
static volatile INT32 g_rwlock080Go[2] = {0, 0};       /* 读者释放命令 */
static volatile INT32 g_rwlock080Unlocked[2] = {0, 0}; /* 读者已解锁标记 */
static volatile INT32 g_rwlock080RdRet[2] = {-1, -1};  /* rdlock 返回值 */

static VOID *Rwlock080Reader(VOID *arg)
{
    INT32 idx = (INT32)(UINTPTR)arg;
    INT32 ret;

    g_rwlock080RdRet[idx] = pthread_rwlock_rdlock(&g_rwl080);
    g_rwlock080Held[idx] = 1;
    /* 持读锁直到主任务发出释放命令 */
    UINT32 i;
    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_rwlock080Go[idx] == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ret = pthread_rwlock_unlock(&g_rwl080);
    if (ret != 0) {
        g_rwlock080RdRet[idx] = ret;
    }
    g_rwlock080Unlocked[idx] = 1;
    return NULL;
}

static UINT32 Rwlock080WaitFlag(volatile INT32 *flag, INT32 expect)
{
    UINT32 i;

    for (i = 0; i < 100; i++) { /* 100: poll bound(100 ticks) */
        if (*flag == expect) {
            return LOS_OK;
        }
        LOS_TaskDelay(1);
    }
    return (*flag == expect) ? LOS_OK : LOS_NOK;
}

static UINT32 Testcase(VOID)
{
    pthread_t reader[2];
    INT32 ret;
    INT32 idx;
    INT32 created = 0;

    for (idx = 0; idx < 2; idx++) { /* 2: two readers */
        g_rwlock080Held[idx] = 0;
        g_rwlock080Go[idx] = 0;
        g_rwlock080Unlocked[idx] = 0;
        g_rwlock080RdRet[idx] = -1;
    }

    ret = pthread_rwlock_init(&g_rwl080, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    for (idx = 0; idx < 2; idx++) { /* 2: two readers */
        ret = pthread_create(&reader[idx], NULL, Rwlock080Reader, (VOID *)(UINTPTR)idx);
        if (ret != 0) {
            ICunitSaveErr(__LINE__, (iiUINT32)ret);
            goto EXIT_JOIN_ALL;
        }
        created++;
    }

    /* 等双读者均持锁 */
    for (idx = 0; idx < 2; idx++) { /* 2: two readers */
        ret = (INT32)Rwlock080WaitFlag(&g_rwlock080Held[idx], 1);
        ICUNIT_GOTO_EQUAL(ret, (INT32)LOS_OK, ret, EXIT_JOIN_ALL);
        ICUNIT_GOTO_EQUAL(g_rwlock080RdRet[idx], 0, g_rwlock080RdRet[idx], EXIT_JOIN_ALL);
    }

    /* 双读者持锁: 写锁争用被拒 */
    ret = pthread_rwlock_trywrlock(&g_rwl080);
    ICUNIT_GOTO_EQUAL(ret, EBUSY, ret, EXIT_JOIN_ALL);

    /* 读者 0 解锁(读者 2→1, unlock :446-448 早返回路径) */
    g_rwlock080Go[0] = 1;
    ret = (INT32)Rwlock080WaitFlag(&g_rwlock080Unlocked[0], 1);
    ICUNIT_GOTO_EQUAL(ret, (INT32)LOS_OK, ret, EXIT_JOIN_ALL);
    ICUNIT_GOTO_EQUAL(g_rwlock080RdRet[0], 0, g_rwlock080RdRet[0], EXIT_JOIN_ALL);

    /* 读者 1 仍持锁: 写锁仍被拒 */
    ret = pthread_rwlock_trywrlock(&g_rwl080);
    ICUNIT_GOTO_EQUAL(ret, EBUSY, ret, EXIT_JOIN_ALL);

    /* 读者 1 也解锁后无持有者: 写锁可获取 */
    g_rwlock080Go[1] = 1;
    ret = (INT32)Rwlock080WaitFlag(&g_rwlock080Unlocked[1], 1);
    ICUNIT_GOTO_EQUAL(ret, (INT32)LOS_OK, ret, EXIT_JOIN_ALL);
    ICUNIT_GOTO_EQUAL(g_rwlock080RdRet[1], 0, g_rwlock080RdRet[1], EXIT_JOIN_ALL);

    ret = pthread_rwlock_trywrlock(&g_rwl080);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_JOIN_ALL);
    ret = pthread_rwlock_unlock(&g_rwl080);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_JOIN_ALL);

    for (idx = 0; idx < 2; idx++) { /* 2: two readers */
        ret = pthread_join(reader[idx], NULL);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_R1);
    }

    ret = pthread_rwlock_destroy(&g_rwl080);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_R1);
    return LOS_OK;
EXIT_JOIN_ALL:
    g_rwlock080Go[0] = 1;
    g_rwlock080Go[1] = 1;
    for (idx = 0; idx < created; idx++) { /* join 已创建的读者 */
        (VOID)pthread_join(reader[idx], NULL);
    }
EXIT_R1:
    (VOID)pthread_rwlock_unlock(&g_rwl080);
    (VOID)pthread_rwlock_destroy(&g_rwl080);
    return LOS_NOK;
}

VOID ItPosixRwlock080(VOID)
{
    TEST_ADD_CASE("ItPosixRwlock080", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
