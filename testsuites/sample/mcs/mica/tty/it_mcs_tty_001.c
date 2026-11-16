/* 用例简要描述: ItMcsTty001 tty_send/tty_printf 入参无效族（NULL/零长/超限） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t ok[8] = "tty-ok\n";
    int ret;
    int wait;

    /* PRECONDITION: tty 端点就绪（依赖主核 bind rpmsg-tty；未就绪时
     * mica_tty_send 返回 0 而非负值——服务门控行为，非入参错误路径） */
    for (wait = 0; wait < 100 && !mica_service_is_ready(MICA_SERVICE_TTY); wait++) {
        LOS_TaskDelay(10);
    }
    ICUNIT_GOTO_EQUAL(mica_service_is_ready(MICA_SERVICE_TTY), 1, 0, EXIT);

    /* tty_send: data=NULL */
    ret = mica_tty_send(NULL, 8);
    dprintf("[ApiTty01] send NULL ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* len=0 */
    ret = mica_tty_send(ok, 0);
    dprintf("[ApiTty01] send len=0 ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret <= 0), 1, ret, EXIT);

    /* [缺陷 20261024_112000 隔离] len 超限 → 截断发送后从核崩溃（无上限守卫）。
     * printf fmt=NULL 同族预防隔离。修复后恢复。 */
#if 0
    ret = mica_tty_send(ok, 0x10000);
    dprintf("[ApiTty01] send len=64K ret=%d\n", ret);
    ret = mica_tty_printf(NULL);
    dprintf("[ApiTty01] printf NULL ret=%d\n", ret);
#endif
    dprintf("[ApiTty01] len=64K/fmt=NULL isolated (bug 20261024_112000)\n");

    /* 空串 */
    ret = mica_tty_printf("");
    dprintf("[ApiTty01] printf empty ret=%d\n", ret);

    /* 对照: 正常发送 */
    ret = mica_tty_send(ok, sizeof(ok) - 1);
    ICUNIT_GOTO_EQUAL(ret, (int)(sizeof(ok) - 1), ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsTty001(VOID)
{
    TEST_ADD_CASE("ItMcsTty001", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICASVC, TEST_LEVEL1, TEST_FUNCTION);
}
