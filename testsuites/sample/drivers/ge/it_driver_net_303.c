/* 用例简要描述: ItDriverNet303 最大帧 1514B 连发 100 帧（长度上限持续压力） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    static uint8_t frame[1514];
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
    (void)memset_s(frame, sizeof(frame), 0xF0, sizeof(frame));

    for (i = 0; i < 100; i++) {
        ret = NetSend(dev, frame, 1514);
        if (ret == -11) {
            busy++;
            LOS_TaskDelay(5);
            continue;
        }
        if (ret != 0) {
            dprintf("[StressNet03] #%u ret=%d\n", i, ret);
            fails++;
            if (fails > 3) {
                break;
            }
        }
    }
    ret = NetGetStats(dev, &st);
    dprintf("[StressNet03] done=%u fails=%d busy=%d txAccepted=%u\n", i, fails, busy, st.txAccepted);
    (VOID)NetStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (VOID)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet303(VOID)
{
    TEST_ADD_CASE("ItDriverNet303", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
