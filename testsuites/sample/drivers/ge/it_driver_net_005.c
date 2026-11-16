/* 用例简要描述: ItDriverNet005 NetStart/NetStop 状态机 errno（未Init/-EIO、幂等、循环） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netDevice *dev = NULL;
    netConfig cfg;
    int ret;

    /* 未 Init 状态：NetStart(NULL) → -EINVAL */
    ret = NetStart(NULL);
    dprintf("[ApiNetState01] start(NULL) ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* READY → Start = 0 */
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* RUNNING 幂等 → 0 */
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* Stop（timeoutUs 给足 100ms）→ 0 回 READY */
    ret = NetStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* READY → 再 Start = 0 */
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)NetStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet005(VOID)
{
    TEST_ADD_CASE("ItDriverNet005", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
