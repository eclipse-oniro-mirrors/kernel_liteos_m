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
 * 用例名称: ItPosixRwlock078
 * 用例描述: 读锁阻塞于写者后被唤醒获取
 *
 * 覆盖目标:
 *   lib/posix/src/pthread_rwlock.c:
 *     pthread_rwlock_timedrdlock :341  time==NULL(rdlock)时写者持有 →
 *       pthread_cond_wait 阻塞分支(现有 77 例无 rdlock 阻塞于写者场景)
 *
 * 断言口径:
 *   写者(主任务)持写锁 → 子任务 rdlock 阻塞 → 主任务解锁 →
 *   子任务获读锁成功(返回 0)并成功解锁; 收尾 destroy 返回 0
 *   (唤醒不依赖时序: 解锁先于 rdlock 时读者直接获取, 两种路径均正确)
 */

/* 用例简要描述: 读锁阻塞于写者后的唤醒获取 */
#include "It_posix_rwlock.h"

static pthread_rwlock_t g_rwl078;
static volatile INT32 g_rwlock078Ready = 0;
static volatile INT32 g_rwlock078Locked = 0;
static volatile INT32 g_rwlock078Unlocked = 0;
static volatile INT32 g_rwlock078RdRet = -1;

static VOID *Rwlock078Reader(VOID *arg)
{
    INT32 ret;

    (VOID)arg;
    g_rwlock078Ready = 1;
    /* 写者持锁期间 rdlock: timedrdlock(time==NULL) → :341 cond_wait 阻塞 */
    g_rwlock078RdRet = pthread_rwlock_rdlock(&g_rwl078);
    g_rwlock078Locked = 1;
    ret = pthread_rwlock_unlock(&g_rwl078);
    if (ret != 0) {
        g_rwlock078RdRet = ret; /* 解锁失败也视为失败 */
    }
    g_rwlock078Unlocked = 1;
    return NULL;
}

static UINT32 Testcase(VOID)
{
    pthread_t reader;
    INT32 ret;
    UINT32 i;

    g_rwlock078Ready = 0;
    g_rwlock078Locked = 0;
    g_rwlock078Unlocked = 0;
    g_rwlock078RdRet = -1;

    ret = pthread_rwlock_init(&g_rwl078, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 主任务持写锁 */
    ret = pthread_rwlock_wrlock(&g_rwl078);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = pthread_create(&reader, NULL, Rwlock078Reader, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_W);

    /* 等读者就绪并让出 2 tick 确保其已阻塞在 rdlock 上 */
    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_rwlock078Ready == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_rwlock078Ready, 1, g_rwlock078Ready, EXIT_JOIN);
    LOS_TaskDelay(2);

    /* 解除写锁: 唤醒阻塞读者(或后续读者直接获取) */
    ret = pthread_rwlock_unlock(&g_rwl078);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_JOIN);

    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_rwlock078Unlocked == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_rwlock078Locked, 1, g_rwlock078Locked, EXIT_JOIN);
    ICUNIT_GOTO_EQUAL(g_rwlock078Unlocked, 1, g_rwlock078Unlocked, EXIT_JOIN);
    ICUNIT_GOTO_EQUAL(g_rwlock078RdRet, 0, g_rwlock078RdRet, EXIT_JOIN);

    ret = pthread_join(reader, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = pthread_rwlock_destroy(&g_rwl078);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT_JOIN:
    (VOID)pthread_join(reader, NULL);
EXIT_W:
    (VOID)pthread_rwlock_unlock(&g_rwl078);
EXIT:
    (VOID)pthread_rwlock_destroy(&g_rwl078);
    return LOS_NOK;
}

VOID ItPosixRwlock078(VOID)
{
    TEST_ADD_CASE("ItPosixRwlock078", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
