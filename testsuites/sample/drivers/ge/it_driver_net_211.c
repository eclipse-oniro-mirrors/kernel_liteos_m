/* 用例简要描述: ItDriverNet211 14B 最小帧发送（D8 下边界 + stats 计数） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    static uint8_t frame[14];
    netStats st;
    int ret;

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

    (void)memset_s(frame, sizeof(frame), 0xBB, sizeof(frame));
    ret = NetSend(dev, frame, 14);
    dprintf("[NetScn01] send 14B ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* GetStats 折叠后计数 */
    ret = NetGetStats(dev, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(st.txAccepted, 1, st.txAccepted, EXIT);
    dprintf("[NetScn01] txAccepted=%u txCompleted=%u\n", st.txAccepted, st.txCompleted);

    (void)NetStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet211(VOID)
{
    TEST_ADD_CASE("ItDriverNet211", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
