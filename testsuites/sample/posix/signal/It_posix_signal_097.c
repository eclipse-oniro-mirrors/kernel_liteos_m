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
 * 用例名称: ItPosixSignal097
 * 用例描述: sigwait 本体跨任务阻塞等待正路径
 *
 * 覆盖目标:
 *   lib/posix/src/signal.c:
 *     sigwait :130-137  LOS_SignalWait 成功 → *sig 回填信号号, 返回 0
 *       (094 只测了 sigwaitinfo; sigwait 本体无正路径用例)
 *
 * 断言口径:
 *   子任务注册 SIGUSR2(内核 Send 的 NO_SET 前置)后阻塞于 sigwait,
 *   主任务 kill(子任务 id, SIGUSR2):
 *   - kill 返回 0
 *   - 子任务 sigwait 返回 0 且 *sig == SIGUSR2
 *   (等待者收到信号走唤醒分支, handler 不执行)
 */

/* 用例简要描述: sigwait 等待被 kill 唤醒返回 */
#include "It_posix_signal.h"
#include <pthread.h>

static volatile INT32 g_sig097Ready = 0;
static volatile INT32 g_sig097Done = 0;
static volatile INT32 g_sig097WaitRet = -1;
static volatile INT32 g_sig097SigNo = 0;

static VOID Sig097Handler(INT32 sig)
{
    (VOID)sig; /* 等待者消费信号, handler 不应执行 */
}

static VOID *Sig097Waiter(VOID *arg)
{
    sigset_t set;
    INT32 sig = 0;

    (VOID)arg;
    /* 前置: 注册 SIGUSR2 置 sigSetFlag, 否则 LOS_SignalSend 受 NO_SET 守卫拒绝 */
    (VOID)signal(SIGUSR2, Sig097Handler);

    (VOID)sigemptyset(&set);
    (VOID)sigaddset(&set, SIGUSR2);

    g_sig097Ready = 1;
    g_sig097WaitRet = sigwait(&set, &sig);
    g_sig097SigNo = sig;
    g_sig097Done = 1;
    return NULL;
}

static UINT32 Testcase(VOID)
{
    pthread_t waiterThread;
    INT32 ret;
    UINT32 i;

    g_sig097Ready = 0;
    g_sig097Done = 0;
    g_sig097WaitRet = -1;
    g_sig097SigNo = 0;

    ret = pthread_create(&waiterThread, NULL, Sig097Waiter, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_sig097Ready == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_sig097Ready, 1, g_sig097Ready, EXIT_JOIN);
    /* ready 置位与真正进入 sigwait 阻塞之间的调度空隙, 再让出 2 tick */
    LOS_TaskDelay(2);

    ret = kill((pid_t)(UINTPTR)waiterThread, SIGUSR2);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_JOIN);

    ret = pthread_join(waiterThread, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* sigwait 正路径: 返回 0 且回填所等信号号 */
    ICUNIT_GOTO_EQUAL(g_sig097WaitRet, 0, g_sig097WaitRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_sig097SigNo, SIGUSR2, g_sig097SigNo, EXIT);
    ICUNIT_GOTO_EQUAL(g_sig097Done, 1, g_sig097Done, EXIT);
    return LOS_OK;
EXIT_JOIN:
    (VOID)pthread_join(waiterThread, NULL);
EXIT:
    return LOS_NOK;
}

VOID ItPosixSignal097(VOID)
{
    TEST_ADD_CASE("ItPosixSignal097", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
