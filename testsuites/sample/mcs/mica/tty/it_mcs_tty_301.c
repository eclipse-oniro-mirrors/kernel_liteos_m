/* 用例简要描述: ItMcsTty301 tty_send 连续 500 次×64B（返回值一致性压力） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t msg[64];
    int i, ret, fails = 0;

    for (i = 0; i < 64; i++) {
        msg[i] = 'A' + (i % 26);
    }
    /* PRECONDITION: tty 端点就绪（未就绪期 send 返回 0=丢帧，非压力失败） */
    {
        int wait;
        for (wait = 0; wait < 100 && !mica_service_is_ready(MICA_SERVICE_TTY); wait++) {
            LOS_TaskDelay(10);
        }
    }
    for (i = 0; i < 500; i++) {
        ret = mica_tty_send(msg, 64);
        if (ret != 64) {
            dprintf("[StressTty01] #%d ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
    }
    dprintf("[StressTty01] done=%d fails=%d (master-side arrival: grep tty in ts.log)\n", i, fails);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsTty301(VOID)
{
    TEST_ADD_CASE("ItMcsTty301", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICASVC, TEST_LEVEL1, TEST_FUNCTION);
}
