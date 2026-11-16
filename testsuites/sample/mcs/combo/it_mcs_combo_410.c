/* 用例简要描述: ItMcsCombo410 Net 本地发送 × NP TCP 代理并发（GE 本地数据面与代理网络双通路，T7） */
#include "it_mcs.h"
#include "net.h"
#ifdef LOSCFG_MCS_NET

#define C10_NET_N  200
#define C10_ECHO_N 30

static netDevice *g_c10Net;
static volatile UINT32 g_c10NetFails, g_c10NetBusy;
static volatile UINT32 g_c10TcpOk;

static VOID C10NetTask(UINT32 arg)
{
    static uint8_t frame[64];
    UINT32 i, busy = 0;
    int ret, fails = 0;

    (void)arg;
    (void)memset_s(frame, sizeof(frame), 0xEE, sizeof(frame));
    for (i = 0; i < C10_NET_N; i++) {
        ret = NetSend(g_c10Net, frame, 64);
        if (ret == -11) {
            busy++;
            LOS_TaskDelay(2);
            continue;
        }
        if (ret != 0) {
            dprintf("[Combo10] net#%u ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
    }
    g_c10NetFails = (UINT32)fails;
    g_c10NetBusy = busy;
}

static VOID C10TcpTask(UINT32 arg)
{
    uint8_t addr[IT_NP_SOCKADDR_LEN];
    uint8_t buf[23] = "combo10-tcp-echo-23B!";
    uint8_t rxb[32];
    int fd, ret, i, fails = 0;

    (void)arg;
    ItNpBuildAddr(addr, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1,
                  IT_NP_ECHO_IP2, IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_TCP);
    fd = np_socket(2, 1, 0);
    if (fd == -1 || np_connect(fd, addr, IT_NP_SOCKADDR_LEN) != 0) {
        dprintf("[Combo10] tcp setup fail fd=%d\n", fd);
        if (fd != -1) {
            (VOID)np_close(fd);
        }
        return;
    }
    for (i = 0; i < C10_ECHO_N; i++) {
        ret = np_send(fd, buf, 23, 0);
        if (ret != 23) {
            dprintf("[Combo10] tcp#%d send=%d\n", i, ret);
            fails++;
            continue;
        }
        ret = np_recv(fd, rxb, sizeof(rxb), 0, 5000);
        if (ret != 23 || memcmp(rxb, buf, 23) != 0) {
            dprintf("[Combo10] tcp#%d recv=%d\n", i, ret);
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    (VOID)np_close(fd);
    if (fails == 0 && i == C10_ECHO_N) {
        g_c10TcpOk = 1;
    }
}

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netStats st;
    TSK_INIT_PARAM_S p = {0};
    UINT32 tidNet, tidTcp;
    UINT32 i;
    int ret;

    g_c10NetFails = 0xFFFFFFFF;
    g_c10NetBusy = 0;
    g_c10TcpOk = 0;

    (void)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.portId = NET_PORT_GMAC1_RK3568;
    cfg.mode = NET_MODE_POLL;
    cfg.autoneg = 0;
    cfg.speedMbps = 100;
    cfg.fullDuplex = 1;
    ret = NetInit(&cfg, &g_c10Net);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetStart(g_c10Net);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_NET);

    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C10NetTask;
    p.pcName = "c10net";
    ret = LOS_TaskCreate(&tidNet, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_NET);
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C10TcpTask;
    p.pcName = "c10tcp";
    ret = LOS_TaskCreate(&tidTcp, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_TASKS);

    for (i = 0; i < 9000; i++) {
        if (g_c10NetFails != 0xFFFFFFFF && g_c10TcpOk) {
            break;
        }
        LOS_TaskDelay(10);
    }
    ret = NetGetStats(g_c10Net, &st);
    dprintf("[Combo10] netFails=%u busy=%u txA=%u txE=%u tcpOk=%u\n",
            g_c10NetFails, g_c10NetBusy, st.txAccepted, st.txErrors, g_c10TcpOk);
    (VOID)LOS_TaskDelete(tidNet);
    (VOID)LOS_TaskDelete(tidTcp);
    (VOID)NetStop(g_c10Net, 100000);
    g_c10Net = NULL;
    ICUNIT_GOTO_EQUAL(g_c10NetFails, 0, (int)g_c10NetFails, EXIT);
    ICUNIT_GOTO_EQUAL(st.txErrors, 0, (int)st.txErrors, EXIT);
    ICUNIT_GOTO_EQUAL(st.txAccepted, (uint32_t)(C10_NET_N - g_c10NetBusy), (int)(st.txAccepted - (C10_NET_N - g_c10NetBusy)), EXIT);
    ICUNIT_GOTO_EQUAL(g_c10TcpOk, 1, (int)g_c10TcpOk, EXIT);
    return LOS_OK;
EXIT_TASKS:
    (VOID)LOS_TaskDelete(tidNet);
EXIT_NET:
    (VOID)NetStop(g_c10Net, 100000);
    g_c10Net = NULL;
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo410(VOID)
{
    TEST_ADD_CASE("ItMcsCombo410", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_MCS_NET */