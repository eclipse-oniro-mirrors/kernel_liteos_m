/* 用例简要描述: McsSvc007 mica_umt_unregister_rcv_cb 注销后转被动收（回调路径先验证） */
#include "it_mcs.h"

static volatile int g_svc013CbCnt;
static char g_svc013CbData[64];
static volatile int g_svc013CbLen;

static void Svc007Cb(const void *data, int data_len, void *priv)
{
    (void)priv;
    if (data_len > (int)sizeof(g_svc013CbData)) {
        data_len = sizeof(g_svc013CbData);
    }
    if (data_len > 0) {
        (void)memcpy_s(g_svc013CbData, sizeof(g_svc013CbData), data, data_len);
    }
    g_svc013CbLen = data_len;
    g_svc013CbCnt++;
}

static UINT32 Testcase(VOID)
{
    uint8_t buf[128];
    size_t len = sizeof(buf);
    int ret;

    /* 1. 回调路径: 注册后 rpc 触发主核按需发一帧（0x304 协议，避免
     * 周期下行耗尽 vring 池——从核 passive 模式 hold buffer 不归还） */
    g_svc013CbCnt = 0;
    ret = mica_umt_register_rcv_cb(Svc007Cb, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    {
        uint32_t rl = 0;
        uint8_t r[8];

        ret = mica_rpc_call(RPC_FUNC_E2E_UMT_DOWN, NULL, 0, r, sizeof(r), &rl, 5000);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    for (len = 0; len < 50 && g_svc013CbCnt == 0; len++) {
        LOS_TaskDelay(100); /* 0.2s 等回调（帧在途） */
    }
    ICUNIT_GOTO_NOT_EQUAL(g_svc013CbCnt, 0, g_svc013CbCnt, EXIT);
    if (g_svc013CbLen < 8 || memcmp(g_svc013CbData, "E2E-UMT", 7) != 0) {
        ICUNIT_GOTO_EQUAL(1, 0, g_svc013CbLen, EXIT);
    }

    /* 2. 注销 → 转被动收（rpc 再触发一帧，mica_rcv_data 阻塞收取） */
    ret = mica_umt_unregister_rcv_cb();
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    {
        uint32_t rl = 0;
        uint8_t r[8];

        ret = mica_rpc_call(RPC_FUNC_E2E_UMT_DOWN, NULL, 0, r, sizeof(r), &rl, 5000);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    len = sizeof(buf);
    ret = mica_rcv_data(buf, &len);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_NOT_EQUAL((uint32_t)len, 0, (int)len, EXIT);
    if (len < 7 || memcmp(buf, "E2E-UMT", 7) != 0) {
        ICUNIT_GOTO_EQUAL(1, 0, (int)len, EXIT);
    }

    /* 3. 注销后可再次注册（生命周期可循环） */
    ret = mica_umt_register_rcv_cb(Svc007Cb, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)mica_umt_unregister_rcv_cb();
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsSystem212(VOID)
{
    TEST_ADD_CASE("ItMcsSystem212", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL0, TEST_FUNCTION);
}
