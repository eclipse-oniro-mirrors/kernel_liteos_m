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
 * 用例名称: ItPosixPthread038
 * 用例描述: 清理缓冲 push/pop 正常流(不经 pthread_exit)
 *
 * 覆盖目标:
 *   lib/posix/src/pthread.c:
 *     pthread_cleanup_pop_inner :91-99(整函数, 现有用例 027 只测了 push+exit,
 *     pop 永不执行——pop 从未被覆盖)
 *       :92  弹出 cancelbuf 链头
 *       :93-96 execute!=0 且 routine!=NULL 时执行 handler
 *
 * 断言口径(在 pthread 上下文内执行, 非 pthread 上下文 __pthread_self 不可用):
 *   - push + pop(0): handler 不执行, 计数不变
 *   - push + pop(1): handler 恰执行一次且收到压栈参数
 */

/* 用例简要描述: 清理缓冲压栈弹栈正常流程 */
#include "It_posix_pthread.h"

#define PTHREAD038_ARG0 ((void *)0x11)
#define PTHREAD038_ARG1 ((void *)0x22)

static volatile INT32 g_pthread038Count = 0;
static volatile VOID *g_pthread038LastArg = NULL;

static VOID Pthread038Handler(VOID *arg)
{
    g_pthread038Count++;
    g_pthread038LastArg = arg;
}

static VOID *Pthread038Thread(VOID *arg)
{
    (VOID)arg;
    /* push 后直接 pop(0): 弹出不执行 handler */
    pthread_cleanup_push(Pthread038Handler, PTHREAD038_ARG0);
    pthread_cleanup_pop(0);

    /* push 后 pop(1): 弹出并执行 handler */
    pthread_cleanup_push(Pthread038Handler, PTHREAD038_ARG1);
    pthread_cleanup_pop(1);

    return NULL;
}

static UINT32 Testcase(VOID)
{
    pthread_t thread;
    INT32 ret;

    g_pthread038Count = 0;
    g_pthread038LastArg = NULL;

    ret = pthread_create(&thread, NULL, Pthread038Thread, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_join(thread, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* pop(0) 不执行 + pop(1) 执行一次: 恰为 1 次, 参数为第二次压栈值 */
    ICUNIT_ASSERT_EQUAL(g_pthread038Count, 1, g_pthread038Count);
    ICUNIT_ASSERT_EQUAL((UINTPTR)g_pthread038LastArg, (UINTPTR)PTHREAD038_ARG1,
                        (UINTPTR)g_pthread038LastArg);

    return LOS_OK;
}

VOID ItPosixPthread038(VOID)
{
    TEST_ADD_CASE("ItPosixPthread038", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
