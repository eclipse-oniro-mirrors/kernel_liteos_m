/* 用例简要描述: ItDriverNet001 NetInit mode 越界/强制速率非法 errno 族 */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;

    /* mode 越界（合法 0-2）→ -EINVAL */
    cfg.mode = 99;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 强制速率模式下 speed=0 → -EINVAL */
    cfg.mode = NET_MODE_POLL;
    cfg.speedMbps = 0;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* speed=50（非 10/100）→ -EINVAL */
    cfg.speedMbps = 50;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 对照: speed=100 → 0 */
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)NetStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet001(VOID)
{
    TEST_ADD_CASE("ItDriverNet001", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
