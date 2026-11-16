/* 用例简要描述: ItMcsRpc002 mica_rpc_call timeout 族（0/1ms/正常值语义） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t req[4] = "api2";
    uint8_t resp[16];
    uint32_t respLen = 0;
    int ret;

    /* timeout=0: 立即返回语义（实测记录：超时错误或立即成功） */
    respLen = 0;
    ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &respLen, 0);
    dprintf("[ApiRpcCall02] timeout=0 ret=%d\n", ret);

    /* timeout=1: 极短超时（可能超时错误也可能赶上回复） */
    respLen = 0;
    ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &respLen, 1);
    dprintf("[ApiRpcCall02] timeout=1 ret=%d\n", ret);

    /* 负 timeout（非 -1）: 行为记录 */
    respLen = 0;
    ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &respLen, -5);
    dprintf("[ApiRpcCall02] timeout=-5 ret=%d\n", ret);

    /* 对照: 正常 timeout 成功 */
    respLen = 0;
    ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &respLen, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc002(VOID)
{
    TEST_ADD_CASE("ItMcsRpc002", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL2, TEST_FUNCTION);
}
