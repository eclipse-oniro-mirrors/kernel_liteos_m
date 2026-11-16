/* 用例简要描述: ItDriverUart004 uart_tx_flush/rx_flush 状态族（未init→-2/正常→0） */
#include "it_driver_uart.h"
#include "uart_drv.h"

#define UART_TEST_PORT 1

static UINT32 Testcase(VOID)
{
    uart_attr_t a;
    uint8_t buf[8] = "flush01";
    int32_t ret;

    a.baudrate = 115200;
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN; /* 合法线制 E-1（N 必须 2 停止位） */
    a.stop_bits = 1;

    /* 未 init → -2 */
    ret = uart_tx_flush(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(ret, -2, ret, EXIT);
    ret = uart_rx_flush(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(ret, -2, ret, EXIT);

    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 写后 tx_flush → 0（阻塞至 TEMT） */
    ret = uart_write(UART_TEST_PORT, buf, 7, 1000);
    ICUNIT_GOTO_EQUAL(ret, 7, ret, EXIT);
    ret = uart_tx_flush(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* rx_flush → 0 */
    ret = uart_rx_flush(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_OK;
EXIT:
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart004(VOID)
{
    TEST_ADD_CASE("ItDriverUart004", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL1, TEST_FUNCTION);
}
