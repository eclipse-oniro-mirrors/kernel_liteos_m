/* 用例简要描述: McsSvc011 mica_tty_send/printf 发送语义（主核侧到达标记执行层核对） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t msg[] = "E2E-TTY-011-mark\n";
    int ret;

    ret = mica_tty_send(msg, sizeof(msg) - 1);
    ICUNIT_GOTO_EQUAL(ret, (int)(sizeof(msg) - 1), ret, EXIT);

    ret = mica_tty_printf("E2E-TTY-011-fmt-%d-%s\n", 42, "ok");
    ICUNIT_GOTO_EQUAL((ret > 0), 1, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsSystem210(VOID)
{
    TEST_ADD_CASE("ItMcsSystem210", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
