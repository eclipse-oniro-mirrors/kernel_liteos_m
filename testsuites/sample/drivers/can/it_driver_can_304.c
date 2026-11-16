/* 用例简要描述: ItDriverCan304 波特率切换循环 500k↔1M×20 档（时序求解稳定性） */
#include "it_driver_can.h"

static UINT32 OneBaud(uint32_t baud)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canFrame f, rx;
    uint32_t j;
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = baud;
    ret = CanInit(&cfg, &dev);
    if (ret != 0) {
        return LOS_NOK;
    }
    ret = CanStart(dev);
    if (ret != 0) {
        (void)CanStop(dev, 100000);
        return LOS_NOK;
    }
    (void)memset_s(&f, sizeof(f), 0, sizeof(f));
    f.id = 0x111;
    f.len = 8;
    (void)memset_s(f.data, 8, 0x5A, 8);
    ret = CanSend(dev, &f, 100000);
    if (ret == 0) {
        for (j = 0; j < 300; j++) {
            if (CanRecv(dev, &rx) == 0) {
                break;
            }
            LOS_TaskDelay(5);
        }
        if (j >= 300 || memcmp(rx.data, f.data, 8) != 0) {
            ret = -1;
        }
    }
    (void)CanStop(dev, 100000);
    return (ret == 0) ? LOS_OK : LOS_NOK;
}

static UINT32 Testcase(VOID)
{
    uint32_t i;
    int fails = 0;

    for (i = 0; i < 20; i++) {
        uint32_t baud = (i & 1) ? 1000000 : 500000;

        if (OneBaud(baud) != LOS_OK) {
            dprintf("[StressCan04] #%u baud=%u FAIL\n", i, baud);
            fails++;
        }
    }
    dprintf("[StressCan04] rounds=20 fails=%d\n", fails);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItDriverCan304(VOID)
{
    TEST_ADD_CASE("ItDriverCan304", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL2, TEST_FUNCTION);
}
