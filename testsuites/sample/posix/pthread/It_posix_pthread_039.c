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
 * 用例名称: ItPosixPthread039
 * 用例描述: 延迟取消在取消点 testcancel 生效退出
 *
 * 覆盖目标:
 *   lib/posix/src/pthread.c:
 *     pthread_cancel    :477-481  DEFERRED 型仅置 canceled=1(不直接删任务)
 *     pthread_testcancel :502     检测到待取消 → pthread_exit(PTHREAD_CANCELED)
 *
 * 断言口径:
 *   子线程循环调用 pthread_testcancel 并周期 LOS_TaskDelay(1)(有界循环),
 *   主任务 cancel 后 join:
 *   - pthread_cancel 返回 0
 *   - join 返回 0 且 retval == PTHREAD_CANCELED(延迟取消在取消点生效)
 *   循环上界 200 次 × 1 tick(约 2s, 有限): 若取消失效则以非 CANCELED
 *     返回值暴露失败, 不死循环。
 *
 * 实现语义偏离说明(对比初版):
 *   初版子线程为 1e6 次纯 testcancel 紧循环, qemu TCG 上可在主任务
 *   LOS_TaskDelay(2) 窗口内跑完整个循环并以哨兵值 0x99 返回, cancel
 *   永远赶不上(实测 RetCode=0x99)。改为每轮 testcancel + LOS_TaskDelay(1),
 *   线程稳定停留在取消点等 cancel 到达, 覆盖目标(DEFERRED 取消点生效)
 *   不变。本实现中 LOS_TaskDelay 非取消点, 取消仍由 testcancel 处生效。
 */

/* 用例简要描述: 延迟取消在取消点生效退出 */
#include "It_posix_pthread.h"

#define PTHREAD039_LOOP_MAX 200 /* 200: bounded loop(200 ticks), cancel 应在早期生效 */

static VOID *Pthread039Target(VOID *arg)
{
    INT32 i;

    (VOID)arg;
    for (i = 0; i < PTHREAD039_LOOP_MAX; i++) {
        pthread_testcancel();      /* 延迟取消点: canceled=1 时 pthread_exit(PTHREAD_CANCELED) */
        LOS_TaskDelay(1); /* 1: tick, 停留在取消点等 cancel, 避免紧循环过早耗尽 */
    }
    return (VOID *)0x99; /* 0x99: 取消失效时的哨兵返回值 */
}

static UINT32 Testcase(VOID)
{
    pthread_t thread;
    VOID *retval = NULL;
    INT32 ret;
    INT32 joined = 0;

    ret = pthread_create(&thread, NULL, Pthread039Target, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 让子线程进入取消点循环 */
    LOS_TaskDelay(2);

    /* DEFERRED 型 cancel: 仅置标记 */
    ret = pthread_cancel(thread);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = pthread_join(thread, &retval);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    joined = 1;
    ICUNIT_GOTO_EQUAL((UINTPTR)retval, (UINTPTR)PTHREAD_CANCELED, (UINTPTR)retval, EXIT_NOJOIN);

    return LOS_OK;
EXIT_NOJOIN:
    /* join 已完成, 不能重复 join(否则打印 not a posix thread 噪声) */
    return LOS_NOK;
EXIT:
    if (joined == 0) {
        (VOID)pthread_join(thread, NULL);
    }
    return LOS_NOK;
}

VOID ItPosixPthread039(VOID)
{
    TEST_ADD_CASE("ItPosixPthread039", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
