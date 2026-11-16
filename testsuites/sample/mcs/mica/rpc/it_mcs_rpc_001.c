/* 用例简要描述: ItMcsRpc001 mica_rpc_call 入参无效族（NULL/零容量/无效func/组合无效） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t req[4] = "api1";
    uint8_t resp[16];
    uint32_t respLen = 0;
    int ret;

    /* func_id 无效段 */
    ret = mica_rpc_call(0xFFFF, req, 4, resp, sizeof(resp), &respLen, 3000);
    dprintf("[ApiRpcCall01] func=0xFFFF ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* req=NULL 且 req_len>0 */
    ret = mica_rpc_call(RPC_FUNC_PING, NULL, 4, resp, sizeof(resp), &respLen, 3000);
    dprintf("[ApiRpcCall01] req=NULL ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 组合无效: func 无效 + req NULL */
    ret = mica_rpc_call(0xFFFF, NULL, 4, resp, sizeof(resp), &respLen, 3000);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 对照: 正常入参（防止全错） */
    respLen = 0;
    ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &respLen, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* [缺陷 20261024_101000 FAIL 在案] resp 侧系统性无守卫（实测均 ret=0）：
     * resp=NULL(cap>0) / resp_cap=0 / resp_len=NULL / resp=NULL+cap=0 组合。
     * 修复后应负返回。整组置尾保证前序断言完整执行。 */
    ret = mica_rpc_call(RPC_FUNC_PING, req, 4, NULL, sizeof(resp), &respLen, 3000);
    dprintf("[ApiRpcCall01] resp=NULL ret=%d (bug 20261024_101000)\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, 0, &respLen, 3000);
    dprintf("[ApiRpcCall01] cap=0 ret=%d (bug 20261024_101000)\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), NULL, 3000);
    dprintf("[ApiRpcCall01] resp_len=NULL ret=%d (bug 20261024_101000)\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    ret = mica_rpc_call(RPC_FUNC_PING, req, 4, NULL, 0, &respLen, 3000);
    dprintf("[ApiRpcCall01] resp=NULL+cap=0 ret=%d (bug 20261024_101000)\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc001(VOID)
{
    TEST_ADD_CASE("ItMcsRpc001", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
