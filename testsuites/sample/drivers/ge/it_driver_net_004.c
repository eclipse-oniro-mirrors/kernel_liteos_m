/* 用例简要描述: ItDriverNet004 NetSend 长度边界与状态 errno（len<14/>1514/非RUNNING） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    static uint8_t frame[1600];
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)memset_s(frame, sizeof(frame), 0xAA, sizeof(frame));

    /* 非 RUNNING → -EIO */
    ret = NetSend(dev, frame, 64);
    ICUNIT_GOTO_EQUAL(ret, -5, ret, EXIT);

    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* len=13（<14，len 越界）→ -EMSGSIZE（设计 §4.2.2：NULL 才 -EINVAL，
     * len 越界统一 -EMSGSIZE；实测与设计一致，首轮断言误写 -22 已修正） */
    ret = NetSend(dev, frame, 13);
    ICUNIT_GOTO_EQUAL(ret, -90, ret, EXIT);

    /* len=1515（>1514）→ -EMSGSIZE */
    ret = NetSend(dev, frame, 1515);
    ICUNIT_GOTO_EQUAL(ret, -90, ret, EXIT);

    /* 对照: len=64 → 0 */
    ret = NetSend(dev, frame, 64);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)NetStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet004(VOID)
{
    TEST_ADD_CASE("ItDriverNet004", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
