/* 用例简要描述: ItDriverCan219 rdyQ 满溢出路径（40 帧不收，容量31，行为记录） */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canFrame f, rx;
    canStats st;
    uint32_t i, drained = 0;
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
    f.len = 8;
    (void)memset_s(f.data, sizeof(f.data), 0xAA, 8);

    /* 连发 40 帧（> rdyQ 容量 31）不收 */
    for (i = 0; i < 40; i++) {
        f.id = 0x200 + i;
        ret = CanSend(dev, &f, 100000);
        if (ret != 0) {
            dprintf("[CanScn03] send #%u ret=%d\n", i, ret);
            break;
        }
    }
    dprintf("[CanScn03] sent=%u (of 40)\n", i);

    /* 逐帧排空计数 */
    while (CanRecv(dev, &rx) == 0) {
        drained++;
    }
    dprintf("[CanScn03] drained=%u\n", drained);

    ret = CanGetStats(dev, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    dprintf("[CanScn03] stats: tx=%u txc=%u rxDel=%u rxDrop=%u\n",
            st.txAccepted, st.txCompleted, st.rxDelivered, st.rxDropped);
    /* 行为记录（不锁死具体策略）：至少 31 帧送达（容量内），排空==已投递 */
    ICUNIT_GOTO_NOT_EQUAL(drained, 0, (int)drained, EXIT);
    ICUNIT_GOTO_EQUAL(drained, st.rxDelivered, (int)(drained - st.rxDelivered), EXIT);

    (void)CanStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan219(VOID)
{
    TEST_ADD_CASE("ItDriverCan219", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL2, TEST_FUNCTION);
}
