/* 用例简要描述: ItDriverNet301 连续发送 1000 帧×64B（ring 吞吐+收割压力） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    static uint8_t frame[64];
    netStats st;
    uint32_t i;
    int ret, fails = 0, busy = 0;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)memset_s(frame, sizeof(frame), 0xEE, sizeof(frame));

    for (i = 0; i < 1000; i++) {
        ret = NetSend(dev, frame, 64);
        if (ret == -11) {
            busy++;
            LOS_TaskDelay(2);
            continue;
        }
        if (ret != 0) {
            dprintf("[StressNet01] #%u ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
    }
    ret = NetGetStats(dev, &st);
    dprintf("[StressNet01] done=%u fails=%d busy=%d txAccepted=%u txErrors=%u\n",
            i, fails, busy, st.txAccepted, st.txErrors);
    (VOID)NetStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    ICUNIT_GOTO_EQUAL(st.txAccepted, (uint32_t)(1000 - busy - fails), (int)(st.txAccepted - (1000 - busy - fails)), EXIT);
    ICUNIT_GOTO_EQUAL(st.txErrors, 0, (int)st.txErrors, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (VOID)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet301(VOID)
{
    TEST_ADD_CASE("ItDriverNet301", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
