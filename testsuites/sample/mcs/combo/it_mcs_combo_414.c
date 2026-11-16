/* 用例简要描述: ItMcsCombo414 七路全并发：RPC+TTY+UMT+NP-TCP+CAN+Net+UART（终点组合压力，全功能） */
#include "it_mcs.h"
#include "can.h"
#include "net.h"
#include "uart_drv.h"
#include "uart_soc_rk3568.h"
#if defined(LOSCFG_MCS_CAN) && defined(LOSCFG_MCS_NET) && defined(LOSCFG_RK3568_UART)

#define C14_PING_N 50
#define C14_TTY_N  100
#define C14_DOWN_N 40
#define C14_ECHO_N 10
#define C14_CAN_N  50
#define C14_NET_N  100
#define C14_UART_N 50
#define UART_TEST_PORT 1
#define UART1_MCR ((volatile uint32_t *)(RK3568_UART1_BASE + 0x10))

static canDevice *g_c14Can;
static netDevice *g_c14Net;
static volatile UINT32 g_c14PingOk, g_c14PingFail;
static volatile UINT32 g_c14TtyFails;
static volatile UINT32 g_c14DownFails, g_c14CbCnt;
static volatile UINT32 g_c14TcpOk;
static volatile UINT32 g_c14CanOk, g_c14CanFails;
static volatile UINT32 g_c14NetFails, g_c14NetBusy;
static volatile UINT32 g_c14UartOk, g_c14UartFails;

static VOID C14UmtCb(const void *data, int data_len, void *priv)
{
    (void)data; (void)priv;
    if (data_len > 0) {
        g_c14CbCnt++;
    }
}

static VOID C14TtyTask(UINT32 arg)
{
    uint8_t msg[64];
    UINT32 i;
    int ret, fails = 0;

    (void)arg;
    for (i = 0; i < 64; i++) {
        msg[i] = 'm' + (i % 7);
    }
    for (i = 0; i < C14_TTY_N; i++) {
        ret = mica_tty_send(msg, 64);
        if (ret != 64) {
            dprintf("[Combo14] tty#%u ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
        LOS_TaskDelay(3);
    }
    g_c14TtyFails = (UINT32)fails;
}

static VOID C14DownTask(UINT32 arg)
{
    uint8_t r[8];
    uint32_t rl;
    int i, ret, fails = 0;

    (void)arg;
    for (i = 0; i < C14_DOWN_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_E2E_UMT_DOWN, NULL, 0, r, sizeof(r), &rl, 8000);
        if (ret != 0) {
            dprintf("[Combo14] down#%d ret=%d\n", i, ret);
            fails++;
            if (fails > 5) {
                break;
            }
        }
        LOS_TaskDelay(5);
    }
    g_c14DownFails = (UINT32)fails;
}

static VOID C14TcpTask(UINT32 arg)
{
    uint8_t addr[IT_NP_SOCKADDR_LEN];
    uint8_t buf[23] = "combo14-tcp-echo-23B!";
    uint8_t rxb[32];
    int fd, ret, i, fails = 0;

    (void)arg;
    ItNpBuildAddr(addr, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1,
                  IT_NP_ECHO_IP2, IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_TCP);
    fd = np_socket(2, 1, 0);
    if (fd == -1 || np_connect(fd, addr, IT_NP_SOCKADDR_LEN) != 0) {
        dprintf("[Combo14] tcp setup fail fd=%d\n", fd);
        if (fd != -1) {
            (VOID)np_close(fd);
        }
        return;
    }
    for (i = 0; i < C14_ECHO_N; i++) {
        ret = np_send(fd, buf, 23, 0);
        if (ret != 23) {
            dprintf("[Combo14] tcp#%d send=%d\n", i, ret);
            fails++;
            continue;
        }
        ret = np_recv(fd, rxb, sizeof(rxb), 0, 5000);
        if (ret != 23 || memcmp(rxb, buf, 23) != 0) {
            dprintf("[Combo14] tcp#%d recv=%d\n", i, ret);
            fails++;
        }
        LOS_TaskDelay(20);
    }
    (VOID)np_close(fd);
    if (fails == 0 && i == C14_ECHO_N) {
        g_c14TcpOk = 1;
    }
}

static VOID C14CanTask(UINT32 arg)
{
    canFrame f, rx;
    UINT32 i, j;
    int ret, fails = 0;

    (void)arg;
    (void)memset_s(&f, sizeof(f), 0, sizeof(f));
    f.len = 8;
    for (i = 0; i < C14_CAN_N; i++) {
        f.id = 0x400 + (i & 0xff);
        for (j = 0; j < 8; j++) {
            f.data[j] = (uint8_t)(i + j);
        }
        ret = CanSend(g_c14Can, &f, 100000);
        if (ret != 0) {
            fails++;
            continue;
        }
        for (j = 0; j < 300; j++) {
            if (CanRecv(g_c14Can, &rx) == 0) {
                break;
            }
            LOS_TaskDelay(5);
        }
        if (j >= 300 || rx.id != f.id || memcmp(rx.data, f.data, 8) != 0) {
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    g_c14CanOk = i;
    g_c14CanFails = (UINT32)fails;
}

static VOID C14NetTask(UINT32 arg)
{
    static uint8_t frame[64];
    UINT32 i, busy = 0;
    int ret, fails = 0;

    (void)arg;
    (void)memset_s(frame, sizeof(frame), 0xEE, sizeof(frame));
    for (i = 0; i < C14_NET_N; i++) {
        ret = NetSend(g_c14Net, frame, 64);
        if (ret == -11) {
            busy++;
            LOS_TaskDelay(2);
            continue;
        }
        if (ret != 0) {
            fails++;
            if (fails > 5) {
                break;
            }
        }
        LOS_TaskDelay(2);
    }
    g_c14NetFails = (UINT32)fails;
    g_c14NetBusy = busy;
}

static VOID C14UartTask(UINT32 arg)
{
    static uint8_t txbuf[32];
    static uint8_t rxbuf[32];
    UINT32 i, j;
    int32_t ret, fails = 0, total;

    (void)arg;
    for (i = 0; i < C14_UART_N; i++) {
        for (j = 0; j < 32; j++) {
            txbuf[j] = (uint8_t)(i + j);
        }
        ret = uart_write(UART_TEST_PORT, txbuf, 32, 3000);
        if (ret != 32) {
            fails++;
            continue;
        }
        total = 0;
        while (total < 32) {
            ret = uart_read(UART_TEST_PORT, rxbuf + total, (uint16_t)(32 - total), 3000);
            if (ret <= 0) {
                break;
            }
            total += ret;
        }
        if (total != 32 || memcmp(rxbuf, txbuf, 32) != 0) {
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    g_c14UartOk = i;
    g_c14UartFails = (UINT32)fails;
}

static UINT32 Testcase(VOID)
{
    canConfig ccfg;
    netConfig ncfg;
    uart_attr_t a;
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid[6];
    UINT32 i;
    int ret, created = 0;
    uint8_t req[4] = "c14p";
    uint8_t resp[8];
    uint32_t rl;

    g_c14PingOk = 0; g_c14PingFail = 0;
    g_c14TtyFails = 0xFFFFFFFF;
    g_c14DownFails = 0xFFFFFFFF; g_c14CbCnt = 0;
    g_c14TcpOk = 0;
    g_c14CanOk = 0; g_c14CanFails = 0xFFFFFFFF;
    g_c14NetFails = 0xFFFFFFFF; g_c14NetBusy = 0;
    g_c14UartOk = 0; g_c14UartFails = 0xFFFFFFFF;

    ret = mica_umt_register_rcv_cb(C14UmtCb, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (void)memset_s(&ccfg, sizeof(ccfg), 0, sizeof(ccfg));
    ccfg.portId = CAN_PORT_CAN1_RK3568;
    ccfg.mode = CAN_MODE_POLL;
    ccfg.workMode = CAN_WORK_LOOPBACK;
    ccfg.bitrate = 500000;
    ret = CanInit(&ccfg, &g_c14Can);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_CB);
    ret = CanStart(g_c14Can);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_CAN);

    (void)memset_s(&ncfg, sizeof(ncfg), 0, sizeof(ncfg));
    ncfg.portId = NET_PORT_GMAC1_RK3568;
    ncfg.mode = NET_MODE_POLL;
    ncfg.autoneg = 0;
    ncfg.speedMbps = 100;
    ncfg.fullDuplex = 1;
    ret = NetInit(&ncfg, &g_c14Net);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_CAN);
    ret = NetStart(g_c14Net);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_DRV2);

    a.baudrate = 115200;
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN;
    a.stop_bits = 1;
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_DRV2);
    *UART1_MCR |= 0x10;

    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C14TtyTask;   p.pcName = "c14tty";
    ret = LOS_TaskCreate(&tid[0], &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV3);
    created++;
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C14DownTask;  p.pcName = "c14down";
    ret = LOS_TaskCreate(&tid[1], &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV3);
    created++;
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C14TcpTask;   p.pcName = "c14tcp";
    ret = LOS_TaskCreate(&tid[2], &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV3);
    created++;
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C14CanTask;   p.pcName = "c14can";
    ret = LOS_TaskCreate(&tid[3], &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV3);
    created++;
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C14NetTask;   p.pcName = "c14net";
    ret = LOS_TaskCreate(&tid[4], &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV3);
    created++;
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C14UartTask;  p.pcName = "c14uart";
    ret = LOS_TaskCreate(&tid[5], &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV3);
    created++;

    /* 本任务: 第 7 路 PING */
    for (i = 0; i < C14_PING_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &rl, 8000);
        if (ret == 0) {
            g_c14PingOk++;
        } else {
            g_c14PingFail++;
            dprintf("[Combo14] ping#%u ret=%d\n", i, ret);
        }
        LOS_TaskDelay(5);
    }
    /* 等六路收尾 */
    for (i = 0; i < 12000; i++) {
        if (g_c14TtyFails != 0xFFFFFFFF && g_c14DownFails != 0xFFFFFFFF &&
            g_c14CanFails != 0xFFFFFFFF && g_c14NetFails != 0xFFFFFFFF &&
            g_c14UartFails != 0xFFFFFFFF && g_c14TcpOk) {
            break;
        }
        LOS_TaskDelay(10);
    }
    for (i = 0; i < 100 && g_c14CbCnt < C14_DOWN_N; i++) {
        LOS_TaskDelay(10);
    }
    dprintf("[Combo14] pingOk=%u/%d | ttyF=%u downF=%u cb=%u/%d tcpOk=%u | canOk=%u canF=%u | netF=%u | uartOk=%u uartF=%u\n",
            g_c14PingOk, C14_PING_N, g_c14TtyFails, g_c14DownFails,
            g_c14CbCnt, C14_DOWN_N, g_c14TcpOk,
            g_c14CanOk, g_c14CanFails, g_c14NetFails, g_c14UartOk, g_c14UartFails);
    for (i = 0; i < (UINT32)created; i++) {
        (VOID)LOS_TaskDelete(tid[i]);
    }
    (VOID)CanStop(g_c14Can, 100000);
    g_c14Can = NULL;
    (VOID)NetStop(g_c14Net, 100000);
    g_c14Net = NULL;
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    (VOID)mica_umt_unregister_rcv_cb();
    ICUNIT_GOTO_EQUAL(g_c14PingOk, C14_PING_N, (int)g_c14PingOk, EXIT);
    ICUNIT_GOTO_EQUAL(g_c14TtyFails, 0, (int)g_c14TtyFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c14DownFails, 0, (int)g_c14DownFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c14CbCnt, C14_DOWN_N, (int)g_c14CbCnt, EXIT);
    ICUNIT_GOTO_EQUAL(g_c14TcpOk, 1, (int)g_c14TcpOk, EXIT);
    ICUNIT_GOTO_EQUAL(g_c14CanFails, 0, (int)g_c14CanFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c14CanOk, C14_CAN_N, (int)g_c14CanOk, EXIT);
    ICUNIT_GOTO_EQUAL(g_c14NetFails, 0, (int)g_c14NetFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c14UartFails, 0, (int)g_c14UartFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c14UartOk, C14_UART_N, (int)g_c14UartOk, EXIT);
    return LOS_OK;
EXIT_DRV3:
    for (i = 0; i < (UINT32)created; i++) {
        (VOID)LOS_TaskDelete(tid[i]);
    }
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
EXIT_DRV2:
    if (g_c14Net != NULL) {
        (VOID)NetStop(g_c14Net, 100000);
        g_c14Net = NULL;
    }
EXIT_CAN:
    if (g_c14Can != NULL) {
        (VOID)CanStop(g_c14Can, 100000);
        g_c14Can = NULL;
    }
EXIT_CB:
    (VOID)mica_umt_unregister_rcv_cb();
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo414(VOID)
{
    TEST_ADD_CASE("ItMcsCombo414", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL2, TEST_FUNCTION);
}

#endif /* defined(LOSCFG_MCS_CAN) && defined(LOSCFG_MCS_NET) && defined(LOSCFG_RK3568_UART) */