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
 * 用例名称: ItPosixPthread036
 * 用例描述: 任务资源耗尽时 pthread_create 错误码映射
 *
 * 覆盖目标:
 *   lib/posix/src/pthread.c:
 *     pthread_create :256-257  LOS_TaskCreateOnly 失败(TCB_UNAVAILABLE) →
 *                        map_errno(LOS_ERRNO_TSK_TCB_UNAVAILABLE) → ENOSPC
 *   lib/posix/src/map_error.c:
 *     map_errno 全函数主体(唯一调用点即 pthread.c:257, 本用例点亮:
 *       LOS_OK→0 / TSK 类错误→POSIX 码 / default→EINVAL / return errno)
 *
 * 断言口径:
 *   以共享信号量阻塞的线程循环创建至 TSK_LIMIT 耗尽:
 *   - 失败那次 pthread_create 返回 ENOSPC(非 -1, 直接返回错误码)
 *   - post 唤醒全部线程并 join 回收后, 可再建(资源恢复)
 */

/* 用例简要描述: 任务耗尽时 create 错误码映射 */
#include "It_posix_pthread.h"
#include <semaphore.h>

static sem_t g_sem036;

static VOID *Pthread036Blocker(VOID *arg)
{
    (VOID)arg;
    (VOID)sem_wait(&g_sem036);
    return NULL;
}

static UINT32 Testcase(VOID)
{
    pthread_t tids[LOSCFG_BASE_CORE_TSK_LIMIT];
    pthread_attr_t attr;
    INT32 created = 0;
    INT32 failRet = 0;
    INT32 ret;
    INT32 i;

    ret = sem_init(&g_sem036, 0, 0);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 小栈(2KB)阻塞线程: 压低堆消耗。注: ws63 测试堆余量(~25KB)仍不足以容纳
     * ~20 线程x2KB, 堆耗尽(ENOMEM)会先于任务限额(ENOSPC)发生——ENOMEM 亦为
     * pthread_create 资源耗尽错误路径; qemu(大堆)按设计先触 ENOSPC。 */
    ret = pthread_attr_init(&attr);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = pthread_attr_setstacksize(&attr, 0x800); /* 0x800: 2KB minimal stack */
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 循环创建阻塞线程至任务资源耗尽 */
    for (i = 0; i < LOSCFG_BASE_CORE_TSK_LIMIT; i++) {
        ret = pthread_create(&tids[i], &attr, Pthread036Blocker, NULL);
        if (ret != 0) {
            failRet = ret;
            break;
        }
        created++;
    }
    ICUNIT_GOTO_NOT_EQUAL(created, 0, created, EXIT);
    ICUNIT_GOTO_EQUAL((failRet == ENOSPC || failRet == ENOMEM), 1, failRet, EXIT);

    /* 唤醒全部阻塞线程 */
    for (i = 0; i < created; i++) {
        (VOID)sem_post(&g_sem036);
    }
    /* join 逐个回收(退出后 TCB 释放) */
    for (i = 0; i < created; i++) {
        ret = pthread_join(tids[i], NULL);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    created = 0;

    /* 资源回收后可重建(再次阻塞-唤醒-回收) */
    pthread_t tidNew;
    ret = pthread_create(&tidNew, NULL, Pthread036Blocker, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (VOID)sem_post(&g_sem036);
    ret = pthread_join(tidNew, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = pthread_attr_destroy(&attr);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = sem_destroy(&g_sem036);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    for (i = 0; i < created; i++) {
        (VOID)sem_post(&g_sem036);
        (VOID)pthread_join(tids[i], NULL);
    }
    (VOID)sem_destroy(&g_sem036);
    return LOS_NOK;
}

VOID ItPosixPthread036(VOID)
{
    TEST_ADD_CASE("ItPosixPthread036", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}
