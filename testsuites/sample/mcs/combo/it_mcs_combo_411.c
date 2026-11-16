/* 用例简要描述: ItMcsCombo411 UMT 0x304 下行 × RPC PING 并发（umt 回调与 rpc 响应同线程竞争，T3，全功能） */
#include "it_mcs.h"

#define C11_DOWN_N 100
#define C11_PING_N 100

static volatile UINT32 g_c11CbCnt;
static volatile UINT32 g_c11PingOk;
static volatile UINT32 g_c11DownFails;

static VOID C11UmtCb(const void *data, int data_len, void *priv)
{
    (void)data; (void)priv;
    if (data_len > 0) {
        g_c11CbCnt++;
    }
}

static VOID C11DownTask(UINT32 arg)
{
    uint8_t r[8];
    uint32_t rl;
    int i, ret, fails = 0;

    (void)arg;
    for (i = 0; i < C11_DOWN_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_E2E_UMT_DOWN, NULL, 0, r, sizeof(r), &rl, 8000);
        if (ret != 0) {
            dprintf("[Combo11] down#%d ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
        LOS_TaskDelay(2); /* 稍拉开下行帧间隔，让回调与 ping 响应真正交错 */
    }
    g_c11DownFails = (UINT32)fails;
}

static UINT32 Testcase(VOID)
{
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid;
    UINT32 i;
    int ret;
    uint8_t req[4] = "c11p";
    uint8_t resp[8];
    uint32_t rl;

    g_c11CbCnt = 0;
    g_c11PingOk = 0;
    g_c11DownFails = 0xFFFFFFFF;

    ret = mica_umt_register_rcv_cb(C11UmtCb, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C11DownTask;
    p.pcName = "c11down";
    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    ret = LOS_TaskCreate(&tid, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_CB);

    /* 本任务: PING 流（响应与 umt 下行帧同由接收线程分派） */
    for (i = 0; i < C11_PING_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &rl, 8000);
        if (ret == 0) {
            g_c11PingOk++;
        } else {
            dprintf("[Combo11] ping#%u ret=%d\n", i, ret);
        }
        LOS_TaskDelay(2);
    }
    for (i = 0; i < 3000; i++) {
        if (g_c11DownFails != 0xFFFFFFFF) {
            break;
        }
        LOS_TaskDelay(10);
    }
    for (i = 0; i < 100 && g_c11CbCnt < C11_DOWN_N; i++) {
        LOS_TaskDelay(10);
    }
    dprintf("[Combo11] pingOk=%u/%d downFails=%u cbCnt=%u/%d\n",
            g_c11PingOk, C11_PING_N, g_c11DownFails, g_c11CbCnt, C11_DOWN_N);
    (VOID)LOS_TaskDelete(tid);
    (VOID)mica_umt_unregister_rcv_cb();
    ICUNIT_GOTO_EQUAL(g_c11PingOk, C11_PING_N, (int)g_c11PingOk, EXIT);
    ICUNIT_GOTO_EQUAL(g_c11DownFails, 0, (int)g_c11DownFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c11CbCnt, C11_DOWN_N, (int)g_c11CbCnt, EXIT);
    return LOS_OK;
EXIT_CB:
    (VOID)mica_umt_unregister_rcv_cb();
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo411(VOID)
{
    TEST_ADD_CASE("ItMcsCombo411", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
