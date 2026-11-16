/* 用例简要描述: McsRpc011 mica_rpc_get_diag 计数一致性（N 次 call 后 rx/tx 增量==N） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    struct mica_rpc_diag d0, d1;
    uint8_t req[8] = "diag11";
    uint8_t resp[16];
    uint32_t respLen = 0;
    uint32_t delta_rx, delta_tx;
    int i, ret;

    mica_rpc_get_diag(&d0);
    for (i = 0; i < 5; i++) {
        respLen = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, sizeof(req),
                            resp, sizeof(resp), &respLen, 5000);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    mica_rpc_get_diag(&d1);
    delta_rx = d1.rx_cnt - d0.rx_cnt;
    delta_tx = d1.tx_cnt - d0.tx_cnt;
    /* 5 次请求 + 5 次响应都走 rpc 端点: rx 增量含响应帧。从核视角:
     * tx = 发出的请求(5), rx = 收到的响应(5)。 */
    ICUNIT_GOTO_EQUAL(delta_tx, 5, (int)delta_tx, EXIT);
    ICUNIT_GOTO_EQUAL(delta_rx, 5, (int)delta_rx, EXIT);
    ICUNIT_GOTO_EQUAL(d1.timeout_cnt - d0.timeout_cnt, 0, (int)(d1.timeout_cnt - d0.timeout_cnt), EXIT);
    ICUNIT_GOTO_EQUAL(d1.drop_cnt - d0.drop_cnt, 0, (int)(d1.drop_cnt - d0.drop_cnt), EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc211(VOID)
{
    TEST_ADD_CASE("ItMcsRpc211", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
