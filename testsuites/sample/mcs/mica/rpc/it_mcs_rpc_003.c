/* 用例简要描述: ItMcsRpc003 mica_rpc_call 容量边界族（resp_cap 恰好/截断/超大） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t req[8] = "cap8byte";
    uint8_t resp[64];
    uint32_t respLen = 0;
    int ret;

    /* PING 回显 8B: cap 恰好 == 响应长（8） */
    respLen = 0;
    (void)memset_s(resp, sizeof(resp), 0, sizeof(resp));
    ret = mica_rpc_call(RPC_FUNC_PING, req, 8, resp, 8, &respLen, 5000);
    dprintf("[ApiRpcCall03] cap==8 ret=%d respLen=%u\n", ret, respLen);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(respLen, 8, respLen, EXIT);

    /* cap 比响应小 1（7）: 截断语义（实测记录 respLen 与返回） */
    respLen = 0;
    (void)memset_s(resp, sizeof(resp), 0, sizeof(resp));
    ret = mica_rpc_call(RPC_FUNC_PING, req, 8, resp, 7, &respLen, 5000);
    dprintf("[ApiRpcCall03] cap=7 ret=%d respLen=%u (truncation)\n", ret, respLen);

    /* cap 超大（64）: 正常 */
    respLen = 0;
    ret = mica_rpc_call(RPC_FUNC_PING, req, 8, resp, sizeof(resp), &respLen, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(respLen, 8, respLen, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc003(VOID)
{
    TEST_ADD_CASE("ItMcsRpc003", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
