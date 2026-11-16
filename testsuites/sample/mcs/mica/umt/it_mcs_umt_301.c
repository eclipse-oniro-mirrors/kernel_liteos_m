/* 用例简要描述: ItMcsUmt301 0x304 下行 200 次（UMT 回调收计数+零泄漏压力） */
#include "it_mcs.h"

static volatile UINT32 g_sumtCnt;

static void SumtCb(const void *data, int data_len, void *priv)
{
    (void)data; (void)priv;
    if (data_len > 0) {
        g_sumtCnt++;
    }
}

static UINT32 Testcase(VOID)
{
    uint32_t rl = 0;
    uint8_t r[8];
    int i, ret, fails = 0;

    g_sumtCnt = 0;
    ret = mica_umt_register_rcv_cb(SumtCb, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 200 次触发主核 0x304 下行帧 → 回调计数（纯下行压力，不混用 send_data
     * 避免 vring buffer 池双向竞争耗尽——第一版 cbCnt=100/200 即此因） */
    for (i = 0; i < 200; i++) {
        ret = mica_rpc_call(RPC_FUNC_E2E_UMT_DOWN, NULL, 0, r, sizeof(r), &rl, 5000);
        if (ret != 0) {
            dprintf("[StressUmt01] #%d down ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
    }
    /* 等回调收尾 */
    for (i = 0; i < 50 && g_sumtCnt < 200; i++) {
        LOS_TaskDelay(10);
    }
    dprintf("[StressUmt01] fails=%d cbCnt=%u/200\n", fails, g_sumtCnt);
    (VOID)mica_umt_unregister_rcv_cb();
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    ICUNIT_GOTO_EQUAL(g_sumtCnt, 200, (int)g_sumtCnt, EXIT);
    return LOS_OK;
EXIT:
    (VOID)mica_umt_unregister_rcv_cb();
    return LOS_NOK;
}

VOID ItMcsUmt301(VOID)
{
    TEST_ADD_CASE("ItMcsUmt301", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICASVC, TEST_LEVEL2, TEST_FUNCTION);
}
