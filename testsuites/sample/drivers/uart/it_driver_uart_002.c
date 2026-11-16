/* 用例简要描述: ItDriverUart002 uart_deinit 状态族（未init/正常/双重/重init幂等） */
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
    ret = uart_deinit(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(ret, -2, ret, EXIT);

    /* init → deinit → 0 */
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = uart_deinit(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 双重 deinit → -2 */
    ret = uart_deinit(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(ret, -2, ret, EXIT);

    /* deinit 后再 init → 0（重 init 幂等，时钟/pinmux 保留） */
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_OK;
EXIT:
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart002(VOID)
{
    TEST_ADD_CASE("ItDriverUart002", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL1, TEST_FUNCTION);
}
