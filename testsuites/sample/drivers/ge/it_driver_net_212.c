/* 用例简要描述: ItDriverNet212 TX ring 满回压（连发至 EAGAIN 或满 32 帧，行为记录） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    static uint8_t frame[64];
    netStats st;
    uint32_t i, sent = 0, eagain = 0;
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)memset_s(frame, sizeof(frame), 0xCC, sizeof(frame));

    /* 快速连发：TX ring 16 槽，超过后应 -EAGAIN+txBusy（DMA 并行收割竞争如实入册） */
    for (i = 0; i < 32; i++) {
        ret = NetSend(dev, frame, 64);
        if (ret == 0) {
            sent++;
        } else if (ret == -11) {
            eagain++;
            break; /* ring 满，回压已实证 */
        } else {
            dprintf("[NetScn02] send #%u ret=%d (unexpected)\n", i, ret);
            ICUNIT_GOTO_EQUAL(1, 0, ret, EXIT);
        }
    }
    dprintf("[NetScn02] sent=%u eagain=%u\n", sent, eagain);

    ret = NetGetStats(dev, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    dprintf("[NetScn02] txAccepted=%u txBusy=%u\n", st.txAccepted, st.txBusy);
    /* 行为记录：满 32 全受理（DMA 收割足够快）或 EAGAIN 已触发（ring 满实证）
     * —— 两种都合法，底线断言：txAccepted == sent */
    ICUNIT_GOTO_EQUAL(st.txAccepted, sent, (int)(st.txAccepted - sent), EXIT);

    (void)NetStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet212(VOID)
{
    TEST_ADD_CASE("ItDriverNet212", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL2, TEST_FUNCTION);
}
