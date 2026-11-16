/* 用例简要描述: ItDriverCan218 交替收发压力 8 轮（全字段+计数一致；连发版因 HW FIFO 深度覆盖改交替） */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canFrame tx[8], rx;
    canStats st;
    uint32_t i, j, got = 0;
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

    /* 交替收发 8 轮（原连发版实测 HW RX FIFO 深度<8 覆盖丢帧——超深部分
     * 由 Scn03 溢出行为覆盖；本用例验证 FIFO 内逐帧正确性） */
    for (i = 0; i < 8; i++) {
        (void)memset_s(&tx[i], sizeof(tx[i]), 0, sizeof(tx[i]));
        tx[i].id = 0x100 + i;
        tx[i].len = 8;
        for (j = 0; j < 8; j++) {
            tx[i].data[j] = (uint8_t)(i * 16 + j);
        }
        ret = CanSend(dev, &tx[i], 100000);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

        for (j = 0; j < 200; j++) {
            if (CanRecv(dev, &rx) == 0) {
                break;
            }
            LOS_TaskDelay(10);
        }
        if (j >= 200) {
            ICUNIT_GOTO_EQUAL(1, 0, (int)i, EXIT); /* 第 i 帧未到 */
        }
        ICUNIT_GOTO_EQUAL(rx.id, tx[i].id, rx.id, EXIT);
        ICUNIT_GOTO_EQUAL(rx.len, tx[i].len, rx.len, EXIT);
        if (memcmp(rx.data, tx[i].data, 8) != 0) {
            ICUNIT_GOTO_EQUAL(1, 0, (int)i, EXIT);
        }
        got++;
    }

    ret = CanGetStats(dev, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(st.txAccepted, 8, st.txAccepted, EXIT);
    ICUNIT_GOTO_EQUAL(st.txCompleted, 8, st.txCompleted, EXIT);
    ICUNIT_GOTO_EQUAL(st.rxDelivered, 8, st.rxDelivered, EXIT);
    ICUNIT_GOTO_EQUAL(st.rxDupSuppressed, 0, st.rxDupSuppressed, EXIT);
    (void)got;
    (void)CanStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan218(VOID)
{
    TEST_ADD_CASE("ItDriverCan218", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
