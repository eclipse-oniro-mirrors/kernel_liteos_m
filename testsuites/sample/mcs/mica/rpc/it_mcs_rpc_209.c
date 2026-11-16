/* 用例简要描述: McsRpc009 register_handler 覆盖式注册（后注册覆盖先注册） */
#include "it_mcs.h"

static volatile int g_rpc009CntA;
static volatile int g_rpc009CntB;

static int Handler009A(uint32_t func_id, const void *req, uint32_t req_len,
                       void *resp, uint32_t resp_cap)
{
    (void)req; (void)req_len;
    if (func_id != RPC_FUNC_E2E_HANDLER) {
        return -95;
    }
    g_rpc009CntA++;
    if (resp_cap >= 4) {
        (void)memcpy_s(resp, resp_cap, "A", 1);
        return 1;
    }
    return 0;
}

static int Handler009B(uint32_t func_id, const void *req, uint32_t req_len,
                       void *resp, uint32_t resp_cap)
{
    (void)req; (void)req_len;
    if (func_id != RPC_FUNC_E2E_HANDLER) {
        return -95;
    }
    g_rpc009CntB++;
    if (resp_cap >= 4) {
        (void)memcpy_s(resp, resp_cap, "B", 1);
        return 1;
    }
    return 0;
}

static UINT32 Testcase(VOID)
{
    struct {
        uint32_t handler_called;
        uint32_t echo_len;
        char echo[48];
    } tr;
    uint32_t trLen = 0;
    int ret;

    g_rpc009CntA = 0;
    g_rpc009CntB = 0;

    ret = mica_rpc_register_handler(Handler009A);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)memset_s(&tr, sizeof(tr), 0, sizeof(tr));
    ret = mica_rpc_call(RPC_FUNC_E2E_TRIGGER, "T1", 2, &tr, sizeof(tr), &trLen, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(g_rpc009CntA, 1, g_rpc009CntA, EXIT);

    ret = mica_rpc_register_handler(Handler009B);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (void)memset_s(&tr, sizeof(tr), 0, sizeof(tr));
    ret = mica_rpc_call(RPC_FUNC_E2E_TRIGGER, "T2", 2, &tr, sizeof(tr), &trLen, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    /* 单 handler 覆盖语义: A 不再被调用，仅 B */
    ICUNIT_GOTO_EQUAL(g_rpc009CntA, 1, g_rpc009CntA, EXIT);
    ICUNIT_GOTO_EQUAL(g_rpc009CntB, 1, g_rpc009CntB, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc209(VOID)
{
    TEST_ADD_CASE("ItMcsRpc209", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
