/* 用例简要描述: ItDriverNet002 GetLinkInfo/GetStats errno（NULL/对照有效快照） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    netLinkInfo info;
    netStats stats;
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* NULL → -EINVAL */
    ret = NetGetLinkInfo(dev, NULL);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);
    ret = NetGetStats(dev, NULL);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 对照: 有效快照（不 assert 速率——台架对端相关，设计 §4.2.3） */
    ret = NetGetLinkInfo(dev, &info);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetGetStats(dev, &stats);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)NetStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet002(VOID)
{
    TEST_ADD_CASE("ItDriverNet002", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
