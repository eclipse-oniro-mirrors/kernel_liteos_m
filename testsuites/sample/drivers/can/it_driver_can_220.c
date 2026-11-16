/* 用例简要描述: ItDriverCan220 canStats 12 字段快照完整性（一轮收发逐字段断言） */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canFrame f, rx;
    canStats st;
    uint32_t i;
    int ret;

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
    f.id = 0x321;
    f.len = 8;
    (void)memset_s(f.data, sizeof(f.data), 0x77, 8);
    ret = CanSend(dev, &f, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    for (i = 0; i < 100; i++) {
        if (CanRecv(dev, &rx) == 0) {
            break;
        }
        LOS_TaskDelay(10);
    }

    /* 12 字段逐项（§4.2.3 字段速查） */
    ret = CanGetStats(dev, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(st.txAccepted, 1, st.txAccepted, EXIT);
    ICUNIT_GOTO_EQUAL(st.txCompleted, 1, st.txCompleted, EXIT);
    ICUNIT_GOTO_EQUAL(st.txErrors, 0, st.txErrors, EXIT);
    ICUNIT_GOTO_EQUAL(st.txTimeouts, 0, st.txTimeouts, EXIT);
    ICUNIT_GOTO_EQUAL(st.rxDelivered, 1, st.rxDelivered, EXIT);
    ICUNIT_GOTO_EQUAL(st.rxDropped, 0, st.rxDropped, EXIT);
    ICUNIT_GOTO_EQUAL(st.rxErrors, 0, st.rxErrors, EXIT);
    ICUNIT_GOTO_EQUAL(st.rxDupSuppressed, 0, st.rxDupSuppressed, EXIT);
    ICUNIT_GOTO_EQUAL(st.busOffCount, 0, st.busOffCount, EXIT);
    ICUNIT_GOTO_EQUAL(st.reserved[0] + st.reserved[1] + st.reserved[2], 0,
                      (int)(st.reserved[0] + st.reserved[1] + st.reserved[2]), EXIT);

    (void)CanStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan220(VOID)
{
    TEST_ADD_CASE("ItDriverCan220", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
