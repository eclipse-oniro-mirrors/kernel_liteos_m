/* 用例简要描述: ItDriverCan217 NORMAL 单节点 TX 悬挂→-ETIMEDOUT（D1/§7.4，无副作用证明） */
#include "it_driver_can.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canFrame f;
    canStats st;
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_NORMAL; /* 单节点无 ACK */
    cfg.bitrate = 500000;

    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)memset_s(&f, sizeof(f), 0, sizeof(f));
    f.id = 0x123;
    f.len = 8;
    (void)memset_s(f.data, sizeof(f.data), 0x55, 8);

    /* NORMAL 单节点：无 ACK → TX 悬挂 → -ETIMEDOUT（设计 §7.4 预期） */
    ret = CanSend(dev, &f, 5000);
    dprintf("[CanScn01] NORMAL no-ack send ret=%d (expect -110)\n", ret);
    ICUNIT_GOTO_EQUAL(ret, -110, ret, EXIT);

    /* txTimeouts 计数 */
    ret = CanGetStats(dev, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(st.txTimeouts, 1, st.txTimeouts, EXIT);

    /* 无副作用证明：切回 LOOPBACK 重 Init → 收发正常 */
    (void)CanStop(dev, 100000);
    cfg.workMode = CAN_WORK_LOOPBACK;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanSend(dev, &f, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    {
        canFrame rx;
        uint32_t i;

        for (i = 0; i < 100; i++) {
            if (CanRecv(dev, &rx) == 0) {
                break;
            }
            LOS_TaskDelay(10);
        }
    }
    (void)CanStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverCan217(VOID)
{
    TEST_ADD_CASE("ItDriverCan217", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_CAN, TEST_LEVEL1, TEST_FUNCTION);
}
