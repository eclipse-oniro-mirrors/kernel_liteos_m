/* 用例简要描述: ItDriverCan301 交替收发 500 轮@500k（持续压力+全等+零dup） */
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
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)memset_s(&f, sizeof(f), 0, sizeof(f));
    f.len = 8;
    for (i = 0; i < 500; i++) {
        f.id = (uint32_t)(0x100 + (i & 0xff));
        for (j = 0; j < 8; j++) {
            f.data[j] = (uint8_t)(i + j);
        }
        ret = CanSend(dev, &f, 100000);
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
        if (j >= 300 || rx.id != f.id || rx.len != f.len || memcmp(rx.data, f.data, 8) != 0) {
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    ret = CanGetStats(dev, &st);
    dprintf("[StressCan01] rounds=%u fails=%d tx=%u rx=%u dup=%u\n",
            i, fails, st.txAccepted, st.rxDelivered, st.rxDupSuppressed);
    (void)CanStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    ICUNIT_GOTO_EQUAL(st.txAccepted, i, (int)(st.txAccepted - i), EXIT);
    ICUNIT_GOTO_EQUAL(st.rxDelivered, i, (int)(st.rxDelivered - i), EXIT);
    ICUNIT_GOTO_EQUAL(st.rxDupSuppressed, 0, (int)st.rxDupSuppressed, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan301(VOID)
{
    TEST_ADD_CASE("ItDriverCan301", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
