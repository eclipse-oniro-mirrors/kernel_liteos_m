/* 用例简要描述: ItDriverNet003 NetRecv 状态 errno（capacity=0/非RUNNING/空队列-EAGAIN） */
#include "it_driver_net.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    uint8_t buf[1600];
    uint32_t len = 0;
    int ret;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 非 RUNNING → -EIO（状态检查优先于 capacity 检查，实测顺序入册） */
    ret = NetRecv(dev, buf, sizeof(buf), &len);
    ICUNIT_GOTO_EQUAL(ret, -5, ret, EXIT);

    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* capacity=0 + 空队列 → -EAGAIN（帧容量检查仅在有帧时执行，行为入册） */
    ret = NetRecv(dev, buf, 0, &len);
    dprintf("[ApiNetRecv01] cap=0(running) ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL(ret, -11, ret, EXIT); /* 实测: cap=0+空队列走 -EAGAIN 路径 */

    /* RUNNING 空队列 → -EAGAIN */
    ret = NetRecv(dev, buf, sizeof(buf), &len);
    ICUNIT_GOTO_EQUAL(ret, -11, ret, EXIT);

    (void)NetStop(dev, 100000);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItDriverNet003(VOID)
{
    TEST_ADD_CASE("ItDriverNet003", Testcase, TEST_LOS, TEST_FN_RK3568SDRVRSV_GMAC, TEST_LEVEL1, TEST_FUNCTION);
}
