/* 用例简要描述: ItMcsRpc005 register_handler 入参族（NULL handler/覆盖后原 handler 不触发） */
#include "it_mcs.h"

static volatile int g_apiHdlrCntA;
static volatile int g_apiHdlrCntB;

static int ApiHdlrA(uint32_t func_id, const void *req, uint32_t req_len,
                    void *resp, uint32_t resp_cap)
{
    (void)req; (void)req_len;
    if (func_id != RPC_FUNC_E2E_HANDLER) {
        return -95;
    }
    g_apiHdlrCntA++;
    if (resp_cap >= 1) {
        ((uint8_t *)resp)[0] = 'A';
        return 1;
    }
    return 0;
}

static int ApiHdlrB(uint32_t func_id, const void *req, uint32_t req_len,
                    void *resp, uint32_t resp_cap)
{
    (void)req; (void)req_len;
    if (func_id != RPC_FUNC_E2E_HANDLER) {
        return -95;
    }
    g_apiHdlrCntB++;
    if (resp_cap >= 1) {
        ((uint8_t *)resp)[0] = 'B';
        return 1;
    }
    return 0;
}

static UINT32 Testcase(VOID)
{
    int ret;

    /* 注册 A → trigger → A 计数 */
    g_apiHdlrCntA = 0;
    g_apiHdlrCntB = 0;
    ret = mica_rpc_register_handler(ApiHdlrA);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    {
        struct {
            uint32_t handler_called;
            uint32_t echo_len;
            char echo[8];
        } tr;
        uint32_t tl = 0;

        (void)memset_s(&tr, sizeof(tr), 0, sizeof(tr));
        ret = mica_rpc_call(RPC_FUNC_E2E_TRIGGER, "T", 1, &tr, sizeof(tr), &tl, 5000);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        ICUNIT_GOTO_EQUAL(g_apiHdlrCntA, 1, g_apiHdlrCntA, EXIT);
    }

    /* 覆盖注册 B → trigger → 仅 B（A 不再触发） */
    ret = mica_rpc_register_handler(ApiHdlrB);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    {
        struct {
            uint32_t handler_called;
            uint32_t echo_len;
            char echo[8];
        } tr;
        uint32_t tl = 0;

        (void)memset_s(&tr, sizeof(tr), 0, sizeof(tr));
        ret = mica_rpc_call(RPC_FUNC_E2E_TRIGGER, "T", 1, &tr, sizeof(tr), &tl, 5000);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        ICUNIT_GOTO_EQUAL(g_apiHdlrCntA, 1, g_apiHdlrCntA, EXIT);
        ICUNIT_GOTO_EQUAL(g_apiHdlrCntB, 1, g_apiHdlrCntB, EXIT);
    }

    /* [缺陷 20261024_101100 FAIL 在案] NULL handler → 实测 ret=0（无守卫，
     * 后续请求分发 NULL 崩溃风险），修复后应 -EINVAL。置末尾。 */
    ret = mica_rpc_register_handler(NULL);
    dprintf("[ApiRpcHdlr01] NULL handler ret=%d (bug 20261024_101100)\n", ret);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc005(VOID)
{
    TEST_ADD_CASE("ItMcsRpc005", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
