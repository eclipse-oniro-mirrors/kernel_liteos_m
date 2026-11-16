/* 用例简要描述: ItDriverNet213 re-Init 循环平滑（D6：Stop→Init→Start ×2，链路不弹跳） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    netLinkInfo info;
    uint32_t round;
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;

    /* 初始 Init+Start */
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* re-Init ×2 轮：Stop → Init → Start，每轮链路快照 valid+up */
    for (round = 1; round <= 2; round++) {
        ret = NetStop(dev, 100000);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        ret = NetInit(&cfg, &dev);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        ret = NetStart(dev);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        ret = NetGetLinkInfo(dev, &info);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        dprintf("[NetScn03] round%u: valid=%u linkUp=%u speed=%u\n",
                round, info.valid, info.linkUp, info.speedMbps);
        ICUNIT_GOTO_EQUAL(info.valid, 1, info.valid, EXIT);
        ICUNIT_GOTO_EQUAL(info.linkUp, 1, info.linkUp, EXIT);
    }

    (void)NetStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet213(VOID)
{
    TEST_ADD_CASE("ItDriverNet213", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
