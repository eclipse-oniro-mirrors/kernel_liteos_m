/* 用例简要描述: McsRpc008 mica_rpc_register_handler 注册与回调触发（E2E_TRIGGER 反向 call 协议） */
#include "it_mcs.h"

static volatile int g_rpc008Called;
static char g_rpc008Echo[64];
static volatile uint32_t g_rpc008EchoLen;

static int Handler008(uint32_t func_id, const void *req, uint32_t req_len,
                      void *resp, uint32_t resp_cap)
{
    uint32_t n;

    if (func_id != RPC_FUNC_E2E_HANDLER) {
        return -95; /* EOPNOTSUPP: 非本用例 func */
    }
    g_rpc008Called++;
    n = (req_len < sizeof(g_rpc008Echo)) ? req_len : sizeof(g_rpc008Echo);
    if (n > 0) {
        (void)memcpy_s(g_rpc008Echo, sizeof(g_rpc008Echo), req, n);
    }
    g_rpc008EchoLen = n;
    n = (n < resp_cap) ? n : resp_cap;
    if (n > 0) {
        (void)memcpy_s(resp, resp_cap, req, n);
    }
    return (int)n;
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

    ret = mica_rpc_register_handler(Handler008);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    g_rpc008Called = 0;
    (void)memset_s(&tr, sizeof(tr), 0, sizeof(tr));
    ret = mica_rpc_call(RPC_FUNC_E2E_TRIGGER, "PING-008", 8,
                        &tr, sizeof(tr), &trLen, 5000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(tr.handler_called, 1, tr.handler_called, EXIT);
    ICUNIT_GOTO_EQUAL(g_rpc008Called, 1, g_rpc008Called, EXIT);
    ICUNIT_GOTO_EQUAL(tr.echo_len, 8, tr.echo_len, EXIT);
    if (memcmp(tr.echo, "PING-008", 8) != 0) {
        ICUNIT_GOTO_EQUAL(1, 0, -1, EXIT);
    }
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc208(VOID)
{
    TEST_ADD_CASE("ItMcsRpc208", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL0, TEST_FUNCTION);
}
