#include "It_posix_signal.h"

static volatile int g_sig087Received = 0;

static void SigHandler087(int sig)
{
    g_sig087Received = sig;
}

/* 用例简要描述: sigprocmask 屏蔽/恢复注册信号 —— 屏蔽期投递被内核拒绝,
   SETMASK 复位后恢复投递 */
static UINT32 Testcase(void)
{
    int ret;
    struct sigaction sa, oldSa;
    sigset_t set, oldSet;

    g_sig087Received = 0;

    sa.sa_handler = SigHandler087;
    sa.sa_flags = 0;
    (void)sigemptyset(&sa.sa_mask);

    ret = sigaction(SIGUSR1, &sa, &oldSa);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = sigemptyset(&set);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_SIG);
    ret = sigaddset(&set, SIGUSR1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_SIG);

    /* 屏蔽 SIGUSR1: 内核 SIG_BLOCK 清其在 sigSetFlag 的注册位(语义对齐
       It_los_signal_005) */
    ret = sigprocmask(SIG_BLOCK, &set, &oldSet);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_SIG);

    /* 屏蔽期投递被拒: raise 失败且 handler 不执行
       (注: BUG_08 位序统一前 sigaddset 置 1U<<sig, 与内核 LOS_SIGNAL_MASK
       位序错位, 本节屏蔽形同虚设, raise 恒成功; 统一为 1U<<(sig-1) 后按
       真实语义断言) */
    ret = raise(SIGUSR1);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_MASK);
    ICUNIT_GOTO_EQUAL(g_sig087Received, 0, g_sig087Received, EXIT_MASK);

    /* SETMASK 恢复后投递恢复: raise 成功且 handler 执行 */
    ret = sigprocmask(SIG_SETMASK, &oldSet, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_MASK);

    ret = raise(SIGUSR1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_MASK);
    ICUNIT_GOTO_EQUAL(g_sig087Received, SIGUSR1, g_sig087Received, EXIT_MASK);

EXIT_MASK:
    sigprocmask(SIG_SETMASK, &oldSet, NULL);
EXIT_SIG:
    sigaction(SIGUSR1, &oldSa, NULL);
EXIT:
    return LOS_OK;
}

VOID ItPosixSignal087(void)
{
    TEST_ADD_CASE("ItPosixSignal087", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}