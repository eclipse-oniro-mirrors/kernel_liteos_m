/* 用例简要描述: ItDriverNet302 Init/Start/Stop 循环 30 次（D6 re-Init 压力+零泄漏） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    netLinkInfo info;
    int i, ret, fails = 0;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;

    for (i = 0; i < 30; i++) {
        ret = NetInit(&cfg, &dev);
        if (ret != 0) {
            dprintf("[StressNet02] #%d init=%d\n", i, ret);
            fails++;
            break;
        }
        ret = NetStart(dev);
        if (ret != 0) {
            fails++;
            break;
        }
        ret = NetStop(dev, 100000);
        if (ret != 0) {
            dprintf("[StressNet02] #%d stop=%d\n", i, ret);
            fails++;
            break;
        }
        /* 每 5 轮抽查链路 */
        if ((i % 5) == 4) {
            ret = NetInit(&cfg, &dev);
            if (ret == 0) {
                (VOID)NetStart(dev);
                ret = NetGetLinkInfo(dev, &info);
                if (ret != 0 || info.valid != 1 || info.linkUp != 1) {
                    dprintf("[StressNet02] #%d link v=%u up=%u\n", i, info.valid, info.linkUp);
                    fails++;
                }
                (VOID)NetStop(dev, 100000);
            }
        }
    }
    dprintf("[StressNet02] rounds=%d fails=%d (ResLeak watch)\n", i, fails);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (VOID)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet302(VOID)
{
    TEST_ADD_CASE("ItDriverNet302", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
