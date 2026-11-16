/* 用例简要描述: McsRpc010 rpc payload 精确边界（446/447/448/449/450/900 分片阈值） */
#include "it_mcs.h"

static UINT32 Testcase(VOID)
{
    static const uint32_t lens[] = {446, 447, 448, 449, 450, 900};
    static uint8_t req[900];
    static uint8_t resp[900];
    uint32_t respLen = 0;
    uint32_t i, j;
    int ret;

    for (i = 0; i < sizeof(req); i++) {
        req[i] = (uint8_t)(i & 0xff);
    }
    for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        respLen = 0;
        (void)memset_s(resp, sizeof(resp), 0, sizeof(resp));
        ret = mica_rpc_call(RPC_FUNC_PING, req, lens[i],
                            resp, sizeof(resp), &respLen, 8000);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        ICUNIT_GOTO_EQUAL(respLen, lens[i], respLen, EXIT);
        for (j = 0; j < lens[i]; j++) {
            if (resp[j] != req[j]) {
                ICUNIT_GOTO_EQUAL(1, 0, (int)j, EXIT);
            }
        }
    }
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc210(VOID)
{
    TEST_ADD_CASE("ItMcsRpc210", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL0, TEST_FUNCTION);
}
