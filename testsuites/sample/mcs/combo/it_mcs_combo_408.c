/* 用例简要描述: ItMcsCombo408 UART1 MCR 内环读写 × RPC PING 并发（UART IRQ 与 rpmsg 并发，T6） */
#include "it_mcs.h"
#include "uart_drv.h"
#include "uart_soc_rk3568.h"
#ifdef LOSCFG_RK3568_UART

#define C8_UART_N 100
#define C8_PING_N 100
#define UART_TEST_PORT 1
#define UART1_MCR ((volatile uint32_t *)(RK3568_UART1_BASE + 0x10))

static volatile UINT32 g_c8PingOk;
static volatile UINT32 g_c8UartOk;
static volatile UINT32 g_c8UartFails;

static VOID C8UartTask(UINT32 arg)
{
    static uint8_t txbuf[32];
    static uint8_t rxbuf[32];
    UINT32 i, j;
    int32_t ret, fails = 0, total;

    (void)arg;
    for (i = 0; i < C8_UART_N; i++) {
        for (j = 0; j < 32; j++) {
            txbuf[j] = (uint8_t)(i + j);
        }
        ret = uart_write(UART_TEST_PORT, txbuf, 32, 3000);
        if (ret != 32) {
            dprintf("[Combo08] uart#%u write=%d\n", i, ret);
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
            dprintf("[Combo08] uart#%u total=%d\n", i, total);
            fails++;
        }
        if (fails > 5) {
            break;
        }
    }
    g_c8UartOk = i;
    g_c8UartFails = (UINT32)fails;
}

static UINT32 Testcase(VOID)
{
    uart_attr_t a;
    TSK_INIT_PARAM_S p = {0};
    UINT32 tid;
    UINT32 i;
    int ret;
    uint8_t req[4] = "c8p";
    uint8_t resp[8];
    uint32_t rl;

    g_c8PingOk = 0;
    g_c8UartOk = 0;
    g_c8UartFails = 0xFFFFFFFF;

    a.baudrate = 115200;
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN;
    a.stop_bits = 1;
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    *UART1_MCR |= 0x10; /* MCR bit4 内环 */

    p.pfnTaskEntry = (TSK_ENTRY_FUNC)C8UartTask;
    p.pcName = "c8uart";
    p.usTaskPrio = 20;
    p.uwStackSize = 0x1000;
    ret = LOS_TaskCreate(&tid, &p);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT_UART);

    /* 本任务: PING 流 */
    for (i = 0; i < C8_PING_N; i++) {
        rl = 0;
        ret = mica_rpc_call(RPC_FUNC_PING, req, 4, resp, sizeof(resp), &rl, 8000);
        if (ret == 0) {
            g_c8PingOk++;
        } else {
            dprintf("[Combo08] ping#%u ret=%d\n", i, ret);
        }
    }
    for (i = 0; i < 6000; i++) {
        if (g_c8UartFails != 0xFFFFFFFF) {
            break;
        }
        LOS_TaskDelay(10);
    }
    dprintf("[Combo08] pingOk=%u/%d uartOk=%u uartFails=%u\n",
            g_c8PingOk, C8_PING_N, g_c8UartOk, g_c8UartFails);
    (VOID)LOS_TaskDelete(tid);
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(g_c8PingOk, C8_PING_N, (int)g_c8PingOk, EXIT);
    ICUNIT_GOTO_EQUAL(g_c8UartFails, 0, (int)g_c8UartFails, EXIT);
    ICUNIT_GOTO_EQUAL(g_c8UartOk, C8_UART_N, (int)g_c8UartOk, EXIT);
    return LOS_OK;
EXIT_UART:
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
EXIT:
    return LOS_NOK;
}

VOID ItMcsCombo408(VOID)
{
    TEST_ADD_CASE("ItMcsCombo408", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_RK3568_UART */