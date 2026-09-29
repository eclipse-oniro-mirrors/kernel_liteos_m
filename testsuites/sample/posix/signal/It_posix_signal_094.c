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


#include "It_posix_signal.h"
#include <pthread.h>

/*
 * BUG_08(20260912 实测定性, 20260923 已修复): posix sigwait 族唤醒 mask 位序错位 ——
 *  1) musl porting liteos_m signal.h sigaddset 原置位 1U<<sig(SIGUSR2=12 -> 0x1000);
 *  2) 内核 los_signal.h:101 LOS_SIGNAL_MASK(sigNo)=1U<<(sigNo-1)(12 -> 0x800);
 *  3) 位序不一致 -> LOS_SignalWait 灌入 sigCB->sigWaitFlag 的位(SignalTimedWait:
 *     los_signal.c:350)与 SignalSend 唤醒判断(los_signal.c:210)所用
 *     LOS_SIGNAL_MASK 位永不重合 -> 等待任务永睡;
 *  4) qemu 内核探针实证 "[094K] WAIT set but mask miss waitFlag=1000 sigNo=12"。
 *
 * 修复方案(已落地, 位序统一为内核/POSIX 标准: sigset 位 = 信号号 - 1):
 *  1) musl porting signal.h sigaddset/sigdelset/sigismember 改置 1U<<(sig-1),
 *     posix 层 set 与内核 LOS_SIGNAL_MASK 位序一致(内核零改动, 含 los_signal.c:396
 *     CLZ 信号号恢复逻辑);
 *  2) lib signal.c sigtimedwait/sigwaitinfo 成功改返回信号号(原误判 -1/EINVAL,
 *     机理见 Testcase094 断言注释; sigwait 改 LOS_WAIT_FOREVER 真阻塞, NULL/空 set
 *     返回 -1/EINVAL);
 * 影响面: sigwait/sigwaitinfo/sigtimedwait 阻塞等待路径(原先等待者永睡);
 * 不受影响: signal() 注册 handler 后 kill 的异步注入模式(090 已验证)。
 * 本用例已恢复执行, 作为回归看护用例常跑; BUG_03(091) 留待复测是否同根消解。
 */
#include <errno.h>

/* 跨任务同步方案: 参照 It_posix_signal_090.c(sender 子任务向主任务发信号)反向使用:
 * 本用例为"主任务 -> 等待子任务"方向, 子任务置 volatile 握手标志 g_sig094Ready
 * (其前完成 signal 注册), 主任务轮询该标志后再让出 2 tick 确保子任务已真正进入
 * sigwaitinfo 阻塞; 完成侧同样轮询 g_sig094WaitDone, 规避硬编码 delay 的调度抖动 */
static volatile int g_sig094Ready = 0;
static volatile int g_sig094WaitDone = 0;

static VOID Sig094Handler(int sig)
{
    (VOID)sig;
}

/**
 * @tc.number    SUB_KERNEL_POSIX_SIGNAL_SIGWAITINFO_001
 * @tc.name      sigwaitinfo cross-task blocking wait normal path
 * @tc.desc      [C- SOFTWARE -0200]
 */
/* 用例简要描述: sigwaitinfo 跨任务阻塞等待正路径(主任务向等待中的子任务投递 SIGUSR2) */
static VOID *WaiterTask094(VOID *arg)
{
    sigset_t set;
    siginfo_t info;
    int ret;

    (VOID)arg;

    /* 前置: 注册 SIGUSR2 置 sigSetFlag, 否则 LOS_SignalSend 受 NO_SET 守卫拒绝
     * (los_signal.c:441); 等待中的任务收到信号走唤醒分支, handler 不会执行 */
    (VOID)signal(SIGUSR2, Sig094Handler);

    (VOID)sigemptyset(&set);
    (VOID)sigaddset(&set, SIGUSR2);
    (VOID)memset_s(&info, sizeof(info), 0, sizeof(info));

    g_sig094Ready = 1;
    /* 无 pending 信号时阻塞: sigwaitinfo -> sigtimedwait(NULL 超时=LOS_WAIT_FOREVER)
     * -> SignalTimedWait 挂入 g_waitSignalList(los_signal.c:353) */
    ret = sigwaitinfo(&set, &info);

    /* F-Pos: POSIX 契约——sigwaitinfo 成功返回所等信号号(位序统一 + 返回语义
     * 修复后成立; 修复前 LOS_SignalWait 成功返回正数信号号, 而 sigtimedwait 以
     * ret==LOS_OK(0) 判成功恒假 -> 误回 -1/EINVAL) */
    ICUNIT_GOTO_EQUAL(ret, SIGUSR2, ret, EXIT);

    /* F-Pos: 成功等到信号后 info 回填所等信号 */
    ICUNIT_GOTO_EQUAL(info.si_signo, SIGUSR2, info.si_signo, EXIT);

    g_sig094WaitDone = 1;

EXIT:
    return NULL;
}

static UINT32 Testcase094(VOID)
{
    int ret;
    pthread_t waiterThread;
    pthread_attr_t waiterAttr;
    struct sched_param sp;
    UINT32 i;

    g_sig094Ready = 0;
    g_sig094WaitDone = 0;

    ret = pthread_attr_init(&waiterAttr);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    sp.sched_priority = LOSCFG_BASE_CORE_TSK_DEFAULT_PRIO;
    (VOID)pthread_attr_setschedparam(&waiterAttr, &sp);
    (VOID)pthread_attr_setinheritsched(&waiterAttr, PTHREAD_EXPLICIT_SCHED);

    ret = pthread_create(&waiterThread, &waiterAttr, WaiterTask094, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_ATTR);

    /* 时序容差: 轮询等待子任务就绪(ready 置位前 signal 注册已完成) */
    for (i = 0; i < 100; i++) {
        if (g_sig094Ready == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_sig094Ready, 1, g_sig094Ready, EXIT_JOIN);
    /* ready 置位与真正进入 sigwaitinfo 阻塞之间的调度空隙, 再让出 2 tick */
    LOS_TaskDelay(2);

    /* F-Pos: 向阻塞等待中的子任务投递 SIGUSR2(唤醒路径 los_signal.c:209-214:
     * sigWaitFlag 命中 -> 回填 sigInfo -> OsSchedWake) */
    ret = pthread_kill(waiterThread, SIGUSR2);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_JOIN);

    /* 等待子任务完成全部断言 */
    for (i = 0; i < 100; i++) {
        if (g_sig094WaitDone == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_sig094WaitDone, 1, g_sig094WaitDone, EXIT_JOIN);

EXIT_JOIN:
    (VOID)pthread_join(waiterThread, NULL);
EXIT_ATTR:
    (VOID)pthread_attr_destroy(&waiterAttr);
    return LOS_OK;
}

/**
 * @tc.number    SUB_KERNEL_POSIX_SIGNAL_SIGWAIT_001
 * @tc.name      sigwait invalid-parameter stub contract
 * @tc.desc      [C- SOFTWARE -0200]
 */
/* 契约固化(修复前为"假成功"): 原 signal.c sigwait 以 timeout=0 调 LOS_SignalWait,
 * SignalTimedWait 见 timeout==0 立即返回 LOS_ERRNO_SIGNAL_INVALID(LOS_ERRNO_OS_FATAL
 * 族 UINT32 大正数), 原 ret<0 判失败恒假 -> 返回 0 且 *sig 恒 0(假成功);
 * 修复后 sigwait 以 LOS_WAIT_FOREVER 真阻塞等待, 对 NULL/sigemptyset 空集在
 * LOS_SignalWait:378 早返 INVALID -> 本层映射 errno=EINVAL 返回 -1 */
/* 用例简要描述: sigwait 非法入参(NULL/空集) -> -1/EINVAL */
VOID ItPosixSignal094(VOID)
{
    TEST_ADD_CASE("ItPosixSignal094", Testcase094, TEST_LIB, TEST_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}

static UINT32 Testcase095(VOID)
{
    sigset_t set;
    int sig;
    int ret;

    /* F-Inv: set=NULL -> LOS_SignalWait:378 早返 INVALID -> 本层映射 -1/EINVAL
       (修复前假成功: 返回 0 且 *sig 被填 0) */
    sig = -1; /* 预置非 0, 观察内核是否真实回填信号号(应保持 -1, 未回填) */
    ret = sigwait(NULL, &sig);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* F-Inv: 空 set(*set==0) -> LOS_SignalWait:378 早返 INVALID -> -1/EINVAL */
    (VOID)sigemptyset(&set);
    sig = -1;
    ret = sigwait(&set, &sig);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    return LOS_OK;
}

VOID ItPosixSignal095(VOID)
{
    TEST_ADD_CASE("ItPosixSignal095", Testcase095, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}