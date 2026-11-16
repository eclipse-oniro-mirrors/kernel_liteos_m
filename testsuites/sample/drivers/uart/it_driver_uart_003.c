/* 用例简要描述: ItDriverUart003 uart_set_attr 入参族（baud/parity/stop 越界/未init/对照） */
#include "it_driver_uart.h"
#include "uart_drv.h"

#define UART_TEST_PORT 1

static UINT32 Testcase(VOID)
{
    uart_attr_t a;
    int32_t ret;

    a.baudrate = 115200;
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN; /* 合法线制 E-1（N 必须 2 停止位） */
    a.stop_bits = 1;

    /* 未 init → -2 */
    ret = uart_set_attr(UART_TEST_PORT, 9600, UART_PARITY_EVEN, 1);
    ICUNIT_GOTO_EQUAL(ret, -2, ret, EXIT);

    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* baud 越界 */
    ret = uart_set_attr(UART_TEST_PORT, 600, UART_PARITY_EVEN, 1);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ret = uart_set_attr(UART_TEST_PORT, 230400, UART_PARITY_EVEN, 1);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* parity 越界 */
    ret = uart_set_attr(UART_TEST_PORT, 9600, (uart_parity_t)99, 1);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* stop 越界 */
    ret = uart_set_attr(UART_TEST_PORT, 9600, UART_PARITY_NONE, 0);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ret = uart_set_attr(UART_TEST_PORT, 9600, UART_PARITY_NONE, 3);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* 对照: 正常改速 → 0 */
    ret = uart_set_attr(UART_TEST_PORT, 9600, UART_PARITY_EVEN, 1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_OK;
EXIT:
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart003(VOID)
{
    TEST_ADD_CASE("ItDriverUart003", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL1, TEST_FUNCTION);
}
