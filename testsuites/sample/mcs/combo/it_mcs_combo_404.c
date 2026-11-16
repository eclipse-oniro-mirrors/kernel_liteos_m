/* 用例简要描述: ItMcsCombo404 tty pty 长命令(help)执行期间 RPC PING 存活（UART 册 D14 指定落点） */
#include "it_mcs.h"

#define C4_PING_N 50

static UINT32 Testcase(VOID)
{
    uint8_t req[4] = "c4p";
    uint8_t resp[8];
    uint32_t rl;
    UINT32 i;
    int ret, fails = 0, to = 0;

    /* 触发主核向 tty pty 注入 "help"（长命令，tty shell 由 prio-20 专用任务异步执行） */
    rl = 0;
    ret = mica_rpc_call(RPC_FUNC_E2E_TTY_EXEC, NULL, 0, resp, sizeof(resp), &rl, 5000);
    dprintf("[Combo04] tty-exec inject ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 立即开 PING 循环，20ms 间隔拉长窗口覆盖命令执行（~1s 窗口） */
    for (i = 0; i < C4_PING_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &rl, 8000);
        if (ret == 0) {
            continue;
        }
        fails++;
        if (ret == -110) { /* -ETIMEDOUT: D14 修复不彻底的信号 */
            to++;
        }
        dprintf("[Combo04] ping#%u ret=%d\n", i, ret);
        LOS_TaskDelay(20);
    }
    dprintf("[Combo04] fails=%d timeouts=%d/%d (console grep '[tty] exec done' as evidence)\n",
            fails, to, C4_PING_N);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo404(VOID)
{
    TEST_ADD_CASE("ItMcsCombo404", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
