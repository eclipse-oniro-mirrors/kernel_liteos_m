/* 用例简要描述: ItDriverCan001 CanInit mode/workMode 越界与时序不可达 errno 族 */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;

    /* mode 越界（合法 0-2） */
    cfg.mode = 99;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* workMode 越界（合法 0-3） */
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = 99;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 时序不可达: 800k@100MHz（设计 §4.2.1 明示示例） */
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 800000;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 对照: 500k 正常 */
    cfg.bitrate = 500000;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)CanStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan001(VOID)
{
    TEST_ADD_CASE("ItDriverCan001", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
