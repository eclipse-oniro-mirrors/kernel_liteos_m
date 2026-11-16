/* 用例简要描述: ItMcsCombo413 RPC × TTY × UMT 三服务并发（T1 三路全共享，全功能） */
#include "it_mcs.h"

#define C13_PING_N 100
#define C13_TTY_N  200
#define C13_DOWN_N 80

static volatile UINT32 g_c13PingOk;
static volatile UINT32 g_c13TtyFails;
static volatile UINT32 g_c13DownFails;
static volatile UINT32 g_c13CbCnt;

static VOID C13UmtCb(const void *data, int data_len, void *priv)
{
    (void)data; (void)priv;
    if (data_len > 0) {
        g_c13CbCnt++;
    }
}

static VOID C13TtyTask(UINT32 arg)
{
    uint8_t msg[64];
    UINT32 i;
    int ret, fails = 0;

    (void)arg;
    for (i = 0; i < 64; i++) {
        msg[i] = 'x' + (i % 3);
    }
    for (i = 0; i < C13_TTY_N; i++) {
        ret = mica_tty_send(msg, 64);
        if (ret != 64) {
            dprintf("[Combo13] tty#%u ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
        LOS_TaskDelay(2);
    }
    g_c13TtyFails = (UINT32)fails;
}

static VOID C13DownTask(UINT32 arg)
{
    uint8_t r[8];
    uint32_t rl;
    int i, ret, fails = 0;

    (void)arg;
    for (i = 0; i < C13_DOWN_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_E2E_UMT_DOWN, NULL, 0, r, sizeof(r), &rl, 8000);
        if (ret != 0) {
            dprintf("[Combo13] down#%d ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
        LOS_TaskDelay(3);
    }
    g_c13DownFails = (UINT32)fails;
}

static UINT32 Testcase(VOID)
{
    struct mica_rpc_diag d0, d1;
    TSK_INIT_PARAM_S p = {0};
    UINT32 tidTty, tidDown;
    UINT32 i;
    int ret;
    uint8_t req[4] = "c13p";
    uint8_t resp[8];
    uint32_t rl;

    g_c13PingOk = 0;
    g_c13TtyFails = 0xFFFFFFFF;
    g_c13DownFails = 0xFFFFFFFF;
    g_c13CbCnt = 0;

    ret = mica_umt_register_rcv_cb(C13UmtCb, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    mica_rpc_get_diag(&d0);

    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C13TtyTask;
    p.pcName = "c13tty";
    ret = LOS_TaskCreate(&tidTty, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_CB);
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C13DownTask;
    p.pcName = "c13down";
    ret = LOS_TaskCreate(&tidDown, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_TASKS);

    /* 本任务: PING 流 */
    for (i = 0; i < C13_PING_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &rl, 8000);
        if (ret == 0) {
            g_c13PingOk++;
        } else {
            dprintf("[Combo13] ping#%u ret=%d\n", i, ret);
        }
        LOS_TaskDelay(2);
    }
    for (i = 0; i < 6000; i++) {
        if (g_c13TtyFails != 0xFFFFFFFF && g_c13DownFails != 0xFFFFFFFF) {
            break;
        }
        LOS_TaskDelay(10);
    }
    for (i = 0; i < 100 && g_c13CbCnt < C13_DOWN_N; i++) {
        LOS_TaskDelay(10);
    }
    mica_rpc_get_diag(&d1);
    dprintf("[Combo13] pingOk=%u/%d ttyFails=%u downFails=%u cbCnt=%u/%d dTo=%u dDrop=%u\n",
            g_c13PingOk, C13_PING_N, g_c13TtyFails, g_c13DownFails,
            g_c13CbCnt, C13_DOWN_N,
            d1.timeout_cnt - d0.timeout_cnt, d1.drop_cnt - d0.drop_cnt);
    (VOID)LOS_TaskDelete(tidTty);
    (VOID)LOS_TaskDelete(tidDown);
    (VOID)mica_umt_unregister_rcv_cb();
    ICUNIT_GOTO_EQUAL(g_c13PingOk, C13_PING_N, (int)g_c13PingOk, EXIT);
    ICUNIT_GOTO_EQUAL(g_c13TtyFails, 0, (int)g_c13TtyFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c13DownFails, 0, (int)g_c13DownFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c13CbCnt, C13_DOWN_N, (int)g_c13CbCnt, EXIT);
    ICUNIT_GOTO_EQUAL(d1.timeout_cnt - d0.timeout_cnt, 0, (int)(d1.timeout_cnt - d0.timeout_cnt), EXIT);
    ICUNIT_GOTO_EQUAL(d1.drop_cnt - d0.drop_cnt, 0, (int)(d1.drop_cnt - d0.drop_cnt), EXIT);
    return LOS_OK;
EXIT_TASKS:
    (VOID)LOS_TaskDelete(tidTty);
EXIT_CB:
    (VOID)mica_umt_unregister_rcv_cb();
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo413(VOID)
{
    TEST_ADD_CASE("ItMcsCombo413", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL2, TEST_FUNCTION);
}
