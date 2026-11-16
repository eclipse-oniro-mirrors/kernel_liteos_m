/* 用例简要描述: ItDriverCan221 FD dataTiming 无解先于寄存器写（失败无副作用证明） */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    int ret;

    /* fdEnable=1 + dataBitrate=800k（数据段 100M/800k=125 无偶 brp 解）→ -EINVAL */
    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;
    cfg.fdEnable = 1;
    cfg.dataBitrate = 800000;
    ret = CanInit(&cfg, &dev);
    dprintf("[CanScn05] fd dataTiming unreachable ret=%d (expect -22)\n", ret);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* 无副作用证明：紧接正常配置（fdEnable=1 + 合法 dataBitrate 2M）→ Init==0 */
    cfg.dataBitrate = 2000000;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
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

VOID ItDriverCan221(VOID)
{
    TEST_ADD_CASE("ItDriverCan221", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
