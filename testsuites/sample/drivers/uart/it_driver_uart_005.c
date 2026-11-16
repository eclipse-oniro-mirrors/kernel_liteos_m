/* 用例简要描述: ItDriverUart005 uart_read/write 错误码族（NULL/未init/负超时/零超时非阻塞） */
#include "it_driver_uart.h"
#include "uart_drv.h"

#define UART_TEST_PORT 1

static UINT32 Testcase(VOID)
{
    uart_attr_t a;
    uint8_t buf[16];
    int32_t ret;

    a.baudrate = 115200;
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN; /* 合法线制 E-1（N 必须 2 停止位） */
    a.stop_bits = 1;

    /* 未 init 状态族 */
    ret = uart_read(UART_TEST_PORT, buf, 16, 100);
    ICUNIT_GOTO_EQUAL(ret, -2, ret, EXIT);
    ret = uart_write(UART_TEST_PORT, buf, 16, 100);
    ICUNIT_GOTO_EQUAL(ret, -2, ret, EXIT);

    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* buf=NULL → -1 */
    ret = uart_read(UART_TEST_PORT, NULL, 16, 100);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ret = uart_write(UART_TEST_PORT, NULL, 16, 100);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* timeout<0 → -4 */
    ret = uart_read(UART_TEST_PORT, buf, 16, -1);
    ICUNIT_GOTO_EQUAL(ret, -4, ret, EXIT);
    ret = uart_write(UART_TEST_PORT, buf, 16, -1);
    ICUNIT_GOTO_EQUAL(ret, -4, ret, EXIT);

    /* timeout=0 非阻塞：无数据读 → 0 字节 */
    ret = uart_read(UART_TEST_PORT, buf, 16, 0);
    dprintf("[ApiUartRw01] nonblock read ret=%d (expect 0)\n", ret);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 对照: 正常写 16B → 16（无回环，TX 到引脚） */
    ret = uart_write(UART_TEST_PORT, buf, 16, 1000);
    ICUNIT_GOTO_EQUAL(ret, 16, ret, EXIT);

    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_OK;
EXIT:
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart005(VOID)
{
    TEST_ADD_CASE("ItDriverUart005", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL1, TEST_FUNCTION);
}
