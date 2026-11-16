/* 用例简要描述: ItMcsCombo402 RPC PING × NP TCP echo 并发（np 与直呼 rpc 竞争同一在途互斥，T2） */
#include "it_mcs.h"

#define C2_PING_N 100
#define C2_ECHO_N 30

static volatile UINT32 g_c2PingOk;

static VOID C2PingTask(UINT32 arg)
{
    uint8_t req[4] = "c2p";
    uint8_t resp[8];
    uint32_t rl;
    int i, ret;

    (void)arg;
    for (i = 0; i < C2_PING_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &rl, 8000);
        if (ret == 0) {
            g_c2PingOk++;
        } else {
            dprintf("[Combo02] ping#%d ret=%d\n", i, ret);
        }
    }
}

static UINT32 Testcase(VOID)
{
    struct mica_rpc_diag d0, d1;
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid;
    UINT32 i;
    int ret, fd, fails = 0;
    uint8_t addr[IT_NP_SOCKADDR_LEN];
    uint8_t buf[23] = "combo02-np-echo-23B!!";
    uint8_t rxb[32];

    g_c2PingOk = 0;
    mica_rpc_get_diag(&d0);

    ItNpBuildAddr(addr, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1,
                  IT_NP_ECHO_IP2, IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_TCP);
    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = np_connect(fd, addr, IT_NP_SOCKADDR_LEN);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_NP);

    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C2PingTask;
    p.pcName = "c2ping";
    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    ret = LOS_TaskCreate(&tid, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_NP);

    /* 本任务: np TCP echo 流量（每次 op 内部都是一次 rpc_call，与 PING 排队） */
    for (i = 0; i < C2_ECHO_N; i++) {
        ret = np_send(fd, buf, 23, 0);
        if (ret != 23) {
            dprintf("[Combo02] echo#%u send=%d\n", i, ret);
            fails++;
            continue;
        }
        ret = np_recv(fd, rxb, sizeof(rxb), 0, 5000);
        if (ret != 23 || memcmp(rxb, buf, 23) != 0) {
            dprintf("[Combo02] echo#%u recv=%d\n", i, ret);
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    for (i = 0; i < 3000; i++) {
        if (g_c2PingOk >= C2_PING_N) {
            break;
        }
        LOS_TaskDelay(10);
    }
    mica_rpc_get_diag(&d1);
    dprintf("[Combo02] echoFails=%d pingOk=%u/%d dTx=%u dRx=%u dTo=%u dDrop=%u\n",
            fails, g_c2PingOk, C2_PING_N,
            d1.tx_cnt - d0.tx_cnt, d1.rx_cnt - d0.rx_cnt,
            d1.timeout_cnt - d0.timeout_cnt, d1.drop_cnt - d0.drop_cnt);
    (VOID)LOS_TaskDelete(tid);
    (VOID)np_close(fd);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c2PingOk, C2_PING_N, (int)g_c2PingOk, EXIT);
    ICUNIT_GOTO_EQUAL(d1.timeout_cnt - d0.timeout_cnt, 0, (int)(d1.timeout_cnt - d0.timeout_cnt), EXIT);
    ICUNIT_GOTO_EQUAL(d1.drop_cnt - d0.drop_cnt, 0, (int)(d1.drop_cnt - d0.drop_cnt), EXIT);
    return LOS_OK;
EXIT:
    (VOID)np_close(fd);
    return LOS_NOK;
EXIT_NP:
    (VOID)np_close(fd);
    return LOS_NOK;
}

VOID ItMcsCombo402(VOID)
{
    TEST_ADD_CASE("ItMcsCombo402", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
