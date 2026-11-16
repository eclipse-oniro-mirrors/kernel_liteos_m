/* 用例简要描述: ItDriverCan302 Init/Start/Stop 循环 50 次（生命周期压力+零泄漏） */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    int i, ret, fails = 0;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;

    for (i = 0; i < 50; i++) {
        ret = CanInit(&cfg, &dev);
        if (ret != 0) {
            dprintf("[StressCan02] #%d init=%d\n", i, ret);
            fails++;
            break;
        }
        ret = CanStart(dev);
        if (ret != 0) {
            fails++;
            break;
        }
        ret = CanStop(dev, 100000);
        if (ret != 0) {
            dprintf("[StressCan02] #%d stop=%d\n", i, ret);
            fails++;
            break;
        }
    }
    dprintf("[StressCan02] rounds=%d fails=%d (ResLeak watch)\n", i, fails);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan302(VOID)
{
    TEST_ADD_CASE("ItDriverCan302", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
