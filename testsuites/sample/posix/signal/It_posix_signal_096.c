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
 * 用例名称: ItPosixSignal096
 * 用例描述: 信号接口参数错误与查询模式回读
 *
 * 覆盖目标:
 *   lib/posix/src/signal.c:
 *     signal      :69-70   非法信号号(>LOS_SIGNAL_SUPPORT_MAX=31) → SIG_ERR + EINVAL
 *     sigaction   :95-96   非法信号号 → -1/EINVAL
 *     sigaction   :102-103 act==NULL 查询模式: 回读原 handler 再回填
 *     sigprocmask :84-85   LOS_SignalMask 失败 → -1/EINVAL
 *   (仅 qemu/ARM: signal 套件注册被 posix_test.c 的 LOSCFG_ARCH_ARM 守卫)
 *
 * 断言口径:
 *   - signal(999, handler) == SIG_ERR 且 errno==EINVAL
 *   - sigaction(999, &act, NULL) == -1 且 errno==EINVAL
 *   - signal(SIGUSR2, H) 注册后 sigaction(SIGUSR2, NULL, &old) == 0
 *     且 old.sa_handler == H(查询模式不改变注册值)
 *   - sigprocmask(999, NULL, NULL) == -1 且 errno==EINVAL(实现拒绝对 set==NULL
 *     的查询, 与 POSIX 惯例差异按现实现断言)
 */

/* 用例简要描述: 信号接口参数错误与查询回读 */
#include "It_posix_signal.h"

static VOID Sig096Handler(INT32 sig)
{
    (VOID)sig;
}

static UINT32 Testcase(VOID)
{
    struct sigaction act = {0};
    struct sigaction old = {0};
    VOID (*prev)(INT32) = NULL;
    INT32 ret;

    /* 1. signal 非法信号号: :69-70 → SIG_ERR/EINVAL */
    errno = 0;
    prev = signal(999, Sig096Handler); /* 999: > LOS_SIGNAL_SUPPORT_MAX(31) */
    ICUNIT_ASSERT_EQUAL((UINTPTR)prev, (UINTPTR)SIG_ERR, (UINTPTR)prev);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 2. sigaction 非法信号号: :95-96 → -1/EINVAL */
    act.sa_handler = Sig096Handler;
    errno = 0;
    ret = sigaction(999, &act, NULL); /* 999: invalid signo */
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 3. sigaction 查询模式: :102-103 act==NULL 回读原 handler */
    prev = signal(SIGUSR2, Sig096Handler);
    ICUNIT_ASSERT_NOT_EQUAL((UINTPTR)prev, (UINTPTR)SIG_ERR, (UINTPTR)prev);
    (VOID)memset_s(&old, sizeof(old), 0, sizeof(old));
    ret = sigaction(SIGUSR2, NULL, &old);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_EQUAL((UINTPTR)old.sa_handler, (UINTPTR)Sig096Handler, (UINTPTR)old.sa_handler);

    /* 4. sigprocmask 参数失败: :84-85 → -1/EINVAL(set==NULL 被实现拒绝) */
    errno = 0;
    ret = sigprocmask(999, NULL, NULL); /* 999: invalid how */
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    return LOS_OK;
}

VOID ItPosixSignal096(VOID)
{
    TEST_ADD_CASE("ItPosixSignal096", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
