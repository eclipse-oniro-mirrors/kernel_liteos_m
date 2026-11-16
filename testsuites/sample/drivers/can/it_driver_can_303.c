/* 用例简要描述: ItDriverCan303 FD 64B 连发 200 帧（FD 数据面持续压力） */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canFrame f, rx;
    canStats st;
    uint32_t i, j;
    int ret, fails = 0;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;
    cfg.fdEnable = 1;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)memset_s(&f, sizeof(f), 0, sizeof(f));
    f.id = 0x3ff;
    f.len = 64;
    f.flags = CAN_FRAME_FDF;
    for (i = 0; i < 200; i++) {
        for (j = 0; j < 64; j++) {
            f.data[j] = (uint8_t)(i + j);
        }
        ret = CanSend(dev, &f, 200000);
        if (ret != 0) {
            fails++;
            continue;
        }
        for (j = 0; j < 300; j++) {
            if (CanRecv(dev, &rx) == 0) {
                break;
            }
            LOS_TaskDelay(5);
        }
        if (j >= 300 || rx.len != 64 || memcmp(rx.data, f.data, 64) != 0) {
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    ret = CanGetStats(dev, &st);
    dprintf("[StressCan03] rounds=%u fails=%d rx=%u\n", i, fails, st.rxDelivered);
    (void)CanStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    ICUNIT_GOTO_EQUAL(st.rxDelivered, i, (int)(st.rxDelivered - i), EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan303(VOID)
{
    TEST_ADD_CASE("ItDriverCan303", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
