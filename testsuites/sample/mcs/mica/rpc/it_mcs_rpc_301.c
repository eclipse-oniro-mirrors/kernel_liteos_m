/* 用例简要描述: ItMcsRpc301 PING 持续往返 1000 次（计数一致性+零泄漏压力） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t req[8] = "stress1!";
    uint8_t resp[8];
    uint32_t respLen = 0;
    struct mica_rpc_diag d0, d1;
    uint32_t delta_tx, delta_rx;
    int i, ret, fails = 0;

    mica_rpc_get_diag(&d0);
    for (i = 0; i < 1000; i++) {
        respLen = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 8, resp, sizeof(resp), &respLen, 8000);
        if (ret != 0 || respLen != 8) {
            dprintf("[StressRpc01] #%d ret=%d len=%u\n", i, ret, respLen);
            fails++;
            if (fails > 5) {
                break;
            }
        }
    }
    mica_rpc_get_diag(&d1);
    delta_tx = d1.tx_cnt - d0.tx_cnt;
    delta_rx = d1.rx_cnt - d0.rx_cnt;
    dprintf("[StressRpc01] done=%d fails=%d dTx=%u dRx=%u dDrop=%u dTmo=%u\n",
            i, fails, delta_tx, delta_rx,
            d1.drop_cnt - d0.drop_cnt, d1.timeout_cnt - d0.timeout_cnt);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    /* diag 计数含并发探活等外部调用：用 >= 下界断言（实测 1000 次全过 dTx=1003） */
    ICUNIT_GOTO_EQUAL((delta_tx >= (uint32_t)i), 1, (int)delta_tx, EXIT);
    ICUNIT_GOTO_EQUAL((delta_rx >= (uint32_t)i), 1, (int)delta_rx, EXIT);
    ICUNIT_GOTO_EQUAL(d1.drop_cnt - d0.drop_cnt, 0, (int)(d1.drop_cnt - d0.drop_cnt), EXIT);
    ICUNIT_GOTO_EQUAL(d1.timeout_cnt - d0.timeout_cnt, 0, (int)(d1.timeout_cnt - d0.timeout_cnt), EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc301(VOID)
{
    TEST_ADD_CASE("ItMcsRpc301", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
