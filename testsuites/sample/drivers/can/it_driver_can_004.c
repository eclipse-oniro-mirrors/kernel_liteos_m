/* 用例简要描述: ItDriverCan004 CanSend 入参/协议 errno（NULL/id 越界/RTR+FD/非RUNNING） */
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
    cfg.fdEnable = 1; /* RTR+FD 组合需 FD 口 */
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)memset_s(&f, sizeof(f), 0, sizeof(f));
    f.id = 0x123;
    f.len = 8;
    (void)memset_s(f.data, sizeof(f.data), 0x55, 8);

    /* 非 RUNNING Send → -EIO */
    ret = CanSend(dev, &f, 100000);
    ICUNIT_GOTO_EQUAL(ret, -5, ret, EXIT);

    /* frame NULL → -EINVAL */
    ret = CanSend(dev, NULL, 100000);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* EFF id 越界（>0x1fffffff）→ -EINVAL */
    f.flags = CAN_FRAME_EFF;
    f.id = 0x20000000;
    ret = CanSend(dev, &f, 100000);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 标准 id 越界（>0x7ff）→ -EINVAL */
    f.flags = 0;
    f.id = 0x800;
    ret = CanSend(dev, &f, 100000);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* RTR + FDF 组合 → -EINVAL */
    f.id = 0x123;
    f.flags = CAN_FRAME_RTR | CAN_FRAME_FDF;
    ret = CanSend(dev, &f, 100000);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 对照: 正常帧 → 0 */
    f.flags = 0;
    f.id = 0x123;
    ret = CanSend(dev, &f, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)CanStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000); /* 防 RUNNING 泄漏污染后续用例 */
    }
    return LOS_NOK;
}

VOID ItDriverCan004(VOID)
{
    TEST_ADD_CASE("ItDriverCan004", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
