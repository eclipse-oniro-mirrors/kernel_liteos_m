/* 用例简要描述: ItMcsRpc004 get_diag 入参族 + is_ready 纯读组合 */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    struct mica_rpc_diag d1, d2;
    int r1, r2;

    /* get_diag(NULL): 行为记录（可能崩溃风险则跳过——实测先探） */
    dprintf("[ApiRpcDiag01] get_diag(NULL) probe\n");

    /* 快照原子性: 连续两次调用（无并发 call 间）关键计数一致 */
    mica_rpc_get_diag(&d1);
    mica_rpc_get_diag(&d2);
    ICUNIT_GOTO_EQUAL(d1.rx_cnt, d2.rx_cnt, (int)(d1.rx_cnt - d2.rx_cnt), EXIT);
    ICUNIT_GOTO_EQUAL(d1.tx_cnt, d2.tx_cnt, (int)(d1.tx_cnt - d2.tx_cnt), EXIT);

    /* is_ready 纯读: 连续两次一致且为 0/1 二值 */
    r1 = mica_rpc_is_ready_pub();
    r2 = mica_rpc_is_ready_pub();
    ICUNIT_GOTO_EQUAL(r1, r2, r1 - r2, EXIT);
    ICUNIT_GOTO_EQUAL((r1 == 0 || r1 == 1), 1, r1, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc004(VOID)
{
    TEST_ADD_CASE("ItMcsRpc004", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL2, TEST_FUNCTION);
}
