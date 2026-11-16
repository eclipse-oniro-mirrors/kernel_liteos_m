/* 用例简要描述: ItDriverNet214 linkInfo 连续采样一致性（BMSR 双读防锁存验证） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    netLinkInfo i1, i2;
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

    /* 连续两次采样：均成功、valid==1、关键状态一致（防锁存双读的稳定性） */
    ret = NetGetLinkInfo(dev, &i1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetGetLinkInfo(dev, &i2);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(i1.valid, 1, i1.valid, EXIT);
    ICUNIT_GOTO_EQUAL(i2.valid, 1, i2.valid, EXIT);
    ICUNIT_GOTO_EQUAL(i1.linkUp, i2.linkUp, (int)(i1.linkUp - i2.linkUp), EXIT);
    ICUNIT_GOTO_EQUAL(i1.speedMbps, i2.speedMbps, (int)(i1.speedMbps - i2.speedMbps), EXIT);
    ICUNIT_GOTO_EQUAL(i1.fullDuplex, i2.fullDuplex, (int)(i1.fullDuplex - i2.fullDuplex), EXIT);
    dprintf("[NetScn04] stable: linkUp=%u speed=%u duplex=%u\n", i1.linkUp, i1.speedMbps, i1.fullDuplex);

    (void)NetStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet214(VOID)
{
    TEST_ADD_CASE("ItDriverNet214", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
