/* 用例简要描述: ItDriverCan005 CanStart/CanStop 状态机 errno（未Init/-EIO、幂等、双停） */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    int ret;

    /* 未 Init 状态：CanStart(NULL) → -EINVAL（canDevice 前向声明不可栈造） */
    ret = CanStart(NULL);
    dprintf("[ApiCanState01] start(NULL) ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* READY → Start = 0 */
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* RUNNING → Start 幂等 = 0 */
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* Stop → 0；再 Stop（READY→STOPPED 释放语义）→ 0 */
    ret = CanStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanStop(dev, 100000);
    dprintf("[ApiCanState01] double stop ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* STOPPED → 再 Start（保留配置免 Init）= 0（015 语义） */
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)CanStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan005(VOID)
{
    TEST_ADD_CASE("ItDriverCan005", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
