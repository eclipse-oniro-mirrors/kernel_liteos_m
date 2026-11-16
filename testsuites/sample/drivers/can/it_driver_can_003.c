/* 用例简要描述: ItDriverCan003 CanRecv 状态机 errno（NULL/非RUNNING/空队列-EAGAIN） */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canFrame f;
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)memset_s(&f, sizeof(f), 0, sizeof(f));

    /* frame NULL → -EINVAL */
    ret = CanRecv(dev, NULL);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 非 RUNNING → -EIO */
    ret = CanRecv(dev, &f);
    ICUNIT_GOTO_EQUAL(ret, -5, ret, EXIT);

    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* RUNNING 空队列 → -EAGAIN（非阻塞） */
    ret = CanRecv(dev, &f);
    ICUNIT_GOTO_EQUAL(ret, -11, ret, EXIT);

    (void)CanStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan003(VOID)
{
    TEST_ADD_CASE("ItDriverCan003", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
