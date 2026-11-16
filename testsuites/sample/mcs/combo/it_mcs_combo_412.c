/* 用例简要描述: ItMcsCombo412 UMT 0x304 下行 × TTY send 并发（tty/umt 双服务并发，T1，全功能） */
#include "it_mcs.h"

#define C12_DOWN_N 100
#define C12_TTY_N  200

static volatile UINT32 g_c12CbCnt;
static volatile UINT32 g_c12TtyFails;
static volatile UINT32 g_c12DownFails;

static VOID C12UmtCb(const void *data, int data_len, void *priv)
{
    (void)data; (void)priv;
    if (data_len > 0) {
        g_c12CbCnt++;
    }
}

static VOID C12DownTask(UINT32 arg)
{
    uint8_t r[8];
    uint32_t rl;
    int i, ret, fails = 0;

    (void)arg;
    for (i = 0; i < C12_DOWN_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_E2E_UMT_DOWN, NULL, 0, r, sizeof(r), &rl, 8000);
        if (ret != 0) {
            dprintf("[Combo12] down#%d ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
        LOS_TaskDelay(2);
    }
    g_c12DownFails = (UINT32)fails;
}

static UINT32 Testcase(VOID)
{
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid;
    UINT32 i;
    int ret, fails = 0;
    uint8_t msg[64];

    for (i = 0; i < 64; i++) {
        msg[i] = 'A' + (i % 26);
    }
    g_c12CbCnt = 0;
    g_c12TtyFails = 0xFFFFFFFF;
    g_c12DownFails = 0xFFFFFFFF;

    ret = mica_umt_register_rcv_cb(C12UmtCb, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C12DownTask;
    p.pcName = "c12down";
    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    ret = LOS_TaskCreate(&tid, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_CB);

    /* 本任务: tty 流 */
    for (i = 0; i < C12_TTY_N; i++) {
        ret = mica_tty_send(msg, 64);
        if (ret != 64) {
            dprintf("[Combo12] tty#%u ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
        LOS_TaskDelay(2);
    }
    g_c12TtyFails = (UINT32)fails;

    for (i = 0; i < 3000; i++) {
        if (g_c12DownFails != 0xFFFFFFFF) {
            break;
        }
        LOS_TaskDelay(10);
    }
    for (i = 0; i < 100 && g_c12CbCnt < C12_DOWN_N; i++) {
        LOS_TaskDelay(10);
    }
    dprintf("[Combo12] ttyFails=%u downFails=%u cbCnt=%u/%d\n",
            g_c12TtyFails, g_c12DownFails, g_c12CbCnt, C12_DOWN_N);
    (VOID)LOS_TaskDelete(tid);
    (VOID)mica_umt_unregister_rcv_cb();
    ICUNIT_GOTO_EQUAL(g_c12TtyFails, 0, (int)g_c12TtyFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c12DownFails, 0, (int)g_c12DownFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c12CbCnt, C12_DOWN_N, (int)g_c12CbCnt, EXIT);
    return LOS_OK;
EXIT_CB:
    (VOID)mica_umt_unregister_rcv_cb();
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo412(VOID)
{
    TEST_ADD_CASE("ItMcsCombo412", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
