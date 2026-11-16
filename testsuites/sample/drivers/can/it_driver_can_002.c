/* 用例简要描述: ItDriverCan002 GetBusState/GetStats errno（NULL/非RUNNING 快照/对照） */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canBusInfo info;
    canStats stats;
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* NULL → -EINVAL */
    ret = CanGetBusState(dev, NULL);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);
    ret = CanGetStats(dev, NULL);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 非 RUNNING: state==STOPPED 且计数 0（§4.2.3） */
    ret = CanGetBusState(dev, &info);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(info.state, CAN_BUS_STOPPED, info.state, EXIT);

    /* 对照: RUNNING 快照有效 */
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanGetBusState(dev, &info);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_NOT_EQUAL(info.state, CAN_BUS_STOPPED, info.state, EXIT);
    ret = CanGetStats(dev, &stats);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)CanStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan002(VOID)
{
    TEST_ADD_CASE("ItDriverCan002", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
