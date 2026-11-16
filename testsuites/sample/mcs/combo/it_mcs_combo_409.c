/* 用例简要描述: ItMcsCombo409 CAN × Net × UART 三驱动并发（纯 T6 驱动共存并发，无 RPC） */
#include "it_mcs.h"
#include "can.h"
#include "net.h"
#include "uart_drv.h"
#include "uart_soc_rk3568.h"
#if defined(LOSCFG_MCS_CAN) && defined(LOSCFG_MCS_NET) && defined(LOSCFG_RK3568_UART)

#define C9_CAN_N  100
#define C9_NET_N  200
#define C9_UART_N 100
#define UART_TEST_PORT 1
#define UART1_MCR ((volatile uint32_t *)(RK3568_UART1_BASE + 0x10))

static canDevice *g_c9Can;
static netDevice *g_c9Net;
static volatile UINT32 g_c9CanOk, g_c9CanFails;
static volatile UINT32 g_c9NetFails, g_c9NetBusy;
static volatile UINT32 g_c9UartOk, g_c9UartFails;

static VOID C9CanTask(UINT32 arg)
{
    canFrame f, rx;
    UINT32 i, j;
    int ret, fails = 0;

    (void)arg;
    (void)memset_s(&f, sizeof(f), 0, sizeof(f));
    f.len = 8;
    for (i = 0; i < C9_CAN_N; i++) {
        f.id = 0x300 + (i & 0xff);
        for (j = 0; j < 8; j++) {
            f.data[j] = (uint8_t)(i + j);
        }
        ret = CanSend(g_c9Can, &f, 100000);
        if (ret != 0) {
            fails++;
            continue;
        }
        for (j = 0; j < 300; j++) {
            if (CanRecv(g_c9Can, &rx) == 0) {
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
    g_c9CanOk = i;
    g_c9CanFails = (UINT32)fails;
}

static VOID C9NetTask(UINT32 arg)
{
    static uint8_t frame[64];
    UINT32 i, busy = 0;
    int ret, fails = 0;

    (void)arg;
    (void)memset_s(frame, sizeof(frame), 0xEE, sizeof(frame));
    for (i = 0; i < C9_NET_N; i++) {
        ret = NetSend(g_c9Net, frame, 64);
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
    }
    g_c9NetFails = (UINT32)fails;
    g_c9NetBusy = busy;
}

static VOID C9UartTask(UINT32 arg)
{
    static uint8_t txbuf[32];
    static uint8_t rxbuf[32];
    UINT32 i, j;
    int32_t ret, fails = 0, total;

    (void)arg;
    for (i = 0; i < C9_UART_N; i++) {
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
    g_c9UartOk = i;
    g_c9UartFails = (UINT32)fails;
}

static UINT32 Testcase(VOID)
{
    canConfig ccfg;
    netConfig ncfg;
    uart_attr_t a;
    canStats cst;
    netStats nst;
    TSK_INIT_PARAM_S p = {0};
    UINT32 tidCan, tidNet, tidUart;
    UINT32 i;
    int ret;

    g_c9CanOk = 0; g_c9CanFails = 0xFFFFFFFF;
    g_c9NetFails = 0xFFFFFFFF; g_c9NetBusy = 0;
    g_c9UartOk = 0; g_c9UartFails = 0xFFFFFFFF;

    (void)memset_s(&ccfg, sizeof(ccfg), 0, sizeof(ccfg));
    ccfg.portId = CAN_PORT_CAN1_RK3568;
    ccfg.mode = CAN_MODE_POLL;
    ccfg.workMode = CAN_WORK_LOOPBACK;
    ccfg.bitrate = 500000;
    ret = CanInit(&ccfg, &g_c9Can);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanStart(g_c9Can);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_CAN);

    (void)memset_s(&ncfg, sizeof(ncfg), 0, sizeof(ncfg));
    ncfg.portId = NET_PORT_GMAC1_RK3568;
    ncfg.mode = NET_MODE_POLL;
    ncfg.autoneg = 0;
    ncfg.speedMbps = 100;
    ncfg.fullDuplex = 1;
    ret = NetInit(&ncfg, &g_c9Net);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_CAN);
    ret = NetStart(g_c9Net);
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
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C9CanTask;
    p.pcName = "c9can";
    ret = LOS_TaskCreate(&tidCan, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV3);
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C9NetTask;
    p.pcName = "c9net";
    ret = LOS_TaskCreate(&tidNet, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV3);
    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C9UartTask;
    p.pcName = "c9uart";
    ret = LOS_TaskCreate(&tidUart, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_DRV3);

    /* 主任务等待三路收尾 */
    for (i = 0; i < 9000; i++) {
        if (g_c9CanFails != 0xFFFFFFFF && g_c9NetFails != 0xFFFFFFFF &&
            g_c9UartFails != 0xFFFFFFFF) {
            break;
        }
        LOS_TaskDelay(10);
    }
    ret = CanGetStats(g_c9Can, &cst);
    ret = NetGetStats(g_c9Net, &nst);
    dprintf("[Combo09] canOk=%u canFails=%u | netFails=%u busy=%u txA=%u txE=%u | uartOk=%u uartFails=%u\n",
            g_c9CanOk, g_c9CanFails, g_c9NetFails, g_c9NetBusy,
            nst.txAccepted, nst.txErrors, g_c9UartOk, g_c9UartFails);
    (VOID)LOS_TaskDelete(tidCan);
    (VOID)LOS_TaskDelete(tidNet);
    (VOID)LOS_TaskDelete(tidUart);
    (VOID)CanStop(g_c9Can, 100000);
    g_c9Can = NULL;
    (VOID)NetStop(g_c9Net, 100000);
    g_c9Net = NULL;
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(g_c9CanFails, 0, (int)g_c9CanFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c9CanOk, C9_CAN_N, (int)g_c9CanOk, EXIT);
    ICUNIT_GOTO_EQUAL(g_c9NetFails, 0, (int)g_c9NetFails, EXIT);
    ICUNIT_GOTO_EQUAL(nst.txErrors, 0, (int)nst.txErrors, EXIT);
    ICUNIT_GOTO_EQUAL(g_c9UartFails, 0, (int)g_c9UartFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c9UartOk, C9_UART_N, (int)g_c9UartOk, EXIT);
    return LOS_OK;
EXIT_DRV3:
    (VOID)LOS_TaskDelete(tidCan);
    (VOID)LOS_TaskDelete(tidNet);
    (VOID)LOS_TaskDelete(tidUart);
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
EXIT_DRV2:
    (VOID)NetStop(g_c9Net, 100000);
    g_c9Net = NULL;
EXIT_CAN:
    (VOID)CanStop(g_c9Can, 100000);
    g_c9Can = NULL;
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo409(VOID)
{
    TEST_ADD_CASE("ItMcsCombo409", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* defined(LOSCFG_MCS_CAN) && defined(LOSCFG_MCS_NET) && defined(LOSCFG_RK3568_UART) */