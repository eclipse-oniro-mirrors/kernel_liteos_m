/* 用例简要描述: ItMcsCombo403 NP TCP × NP UDP 双 fd 并发（np 多 socket 并发排队，T2） */
#include "it_mcs.h"

#define C3_TCP_N 30
#define C3_UDP_N 30

static volatile UINT32 g_c3TcpOk;

static VOID C3TcpTask(UINT32 arg)
{
    uint8_t addr[IT_NP_SOCKADDR_LEN];
    uint8_t buf[23] = "combo03-tcp-echo-23B!";
    uint8_t rxb[32];
    int fd, ret, i, fails = 0;

    (void)arg;
    ItNpBuildAddr(addr, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1,
                  IT_NP_ECHO_IP2, IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_TCP);
    fd = np_socket(2, 1, 0);
    if (fd == -1) {
        dprintf("[Combo03] tcp socket fail\n");
        return;
    }
    if (np_connect(fd, addr, IT_NP_SOCKADDR_LEN) != 0) {
        dprintf("[Combo03] tcp connect fail\n");
        (VOID)np_close(fd);
        return;
    }
    for (i = 0; i < C3_TCP_N; i++) {
        ret = np_send(fd, buf, 23, 0);
        if (ret != 23) {
            dprintf("[Combo03] tcp#%d send=%d\n", i, ret);
            fails++;
            continue;
        }
        ret = np_recv(fd, rxb, sizeof(rxb), 0, 5000);
        if (ret != 23 || memcmp(rxb, buf, 23) != 0) {
            dprintf("[Combo03] tcp#%d recv=%d\n", i, ret);
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    (VOID)np_close(fd);
    dprintf("[Combo03] tcpDone=%d tcpFails=%d\n", i, fails);
    if (fails == 0 && i == C3_TCP_N) {
        g_c3TcpOk = 1;
    }
}

static UINT32 Testcase(VOID)
{
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid;
    UINT32 i;
    int ret, fd, fails = 0;
    uint8_t addr[IT_NP_SOCKADDR_LEN];
    uint8_t buf[23] = "combo03-udp-echo-23B!";
    uint8_t rxb[32];
    uint32_t fromLen;

    g_c3TcpOk = 0;
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C3TcpTask;
    p.pcName = "c3tcp";
    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    ret = LOS_TaskCreate(&tid, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 本任务: UDP echo */
    ItNpBuildAddr(addr, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1,
                  IT_NP_ECHO_IP2, IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_UDP);
    fd = np_socket(2, 2, 0);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    for (i = 0; i < C3_UDP_N; i++) {
        ret = np_sendto(fd, buf, 23, 0, addr, IT_NP_SOCKADDR_LEN);
        if (ret != 23) {
            dprintf("[Combo03] udp#%u send=%d\n", i, ret);
            fails++;
            continue;
        }
        fromLen = IT_NP_SOCKADDR_LEN;
        ret = np_recvfrom(fd, rxb, sizeof(rxb), 0, 5000, addr, &fromLen);
        if (ret != 23 || memcmp(rxb, buf, 23) != 0) {
            dprintf("[Combo03] udp#%u recv=%d\n", i, ret);
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    (VOID)np_close(fd);
    for (i = 0; i < 3000; i++) {
        if (g_c3TcpOk) {
            break;
        }
        LOS_TaskDelay(10);
    }
    dprintf("[Combo03] udpFails=%d tcpOk=%u\n", fails, g_c3TcpOk);
    (VOID)LOS_TaskDelete(tid);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c3TcpOk, 1, (int)g_c3TcpOk, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo403(VOID)
{
    TEST_ADD_CASE("ItMcsCombo403", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}
