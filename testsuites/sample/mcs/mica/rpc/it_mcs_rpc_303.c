/* 用例简要描述: ItMcsRpc303 最大包长 448B 连续 300 次（分片路径持续压力） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    static uint8_t req[448];
    static uint8_t resp[448];
    uint32_t respLen = 0;
    int i, j, ret, fails = 0;

    for (j = 0; j < 448; j++) {
        req[j] = (uint8_t)(j & 0xff);
    }
    for (i = 0; i < 300; i++) {
        respLen = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 448, resp, sizeof(resp), &respLen, 10000);
        if (ret != 0 || respLen != 448) {
            dprintf("[StressRpc03] #%d ret=%d len=%u\n", i, ret, respLen);
            fails++;
            if (fails > 3) {
                break;
            }
        }
    }
    dprintf("[StressRpc03] done=%d fails=%d\n", i, fails);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc303(VOID)
{
    TEST_ADD_CASE("ItMcsRpc303", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
