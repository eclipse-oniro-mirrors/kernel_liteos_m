/* 用例简要描述: ItDriverUart302 init/deinit 循环 100 次（mux leak 缺陷 20261024_170000 量化实证：当前预期 ResLeak 增量=100） */
#include "it_driver_uart.h"
#include "uart_drv.h"

#define UART_TEST_PORT 1

static UINT32 Testcase(VOID)
{
    uart_attr_t a;
    int i, ret, fails = 0;

    a.baudrate = 115200;
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN;
    a.stop_bits = 1;

    for (i = 0; i < 100; i++) {
        ret = uart_init(UART_TEST_PORT, &a);
        if (ret != 0) {
            dprintf("[StressUart02] #%d init=%d\n", i, ret);
            fails++;
            break;
        }
        ret = uart_deinit(UART_TEST_PORT);
        if (ret != 0) {
            dprintf("[StressUart02] #%d deinit=%d\n", i, ret);
            fails++;
            break;
        }
    }
    /* [问题 20261024_170000 已反馈] ResLeak 观测点：本轮预期 after-before==100
     * （每次 init 泄漏 1 mux）；缺陷修复后应==0。断言本轮 API 返回值全部正确，
     * 泄漏由框架 ResLeak 输出量化（不在此断言泄漏数——ResLeak 报告在用例外）。 */
    dprintf("[StressUart02] rounds=%d fails=%d (ResLeak delta expected: 100 pre-fix / 0 post-fix)\n", i, fails);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    return LOS_OK;
EXIT:
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart302(VOID)
{
    TEST_ADD_CASE("ItDriverUart302", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL2, TEST_FUNCTION);
}
