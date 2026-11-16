/* 用例简要描述: ItDriverUart001 uart_init 入参族（NULL/字段越界/port 越界/UART0/重复init） */
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

    /* attr=NULL → -1 */
    ret = uart_init(UART_TEST_PORT, NULL);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* baud 越界 */
    a.baudrate = 600;
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    a.baudrate = 230400;
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* data_bits 越界 */
    a.baudrate = 115200;
    a.data_bits = 4;
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    a.data_bits = 9;
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* parity 越界 */
    a.data_bits = 8;
    a.parity = (uart_parity_t)99;
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* stop_bits 越界（NONE+1 亦属线制非法，但 stop=0/3 在组合检查前命中） */
    a.parity = UART_PARITY_EVEN;
    a.stop_bits = 0;
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    a.stop_bits = 3;
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* port=0 (UART0 不可达) → -4 */
    a.stop_bits = 1;
    ret = uart_init(0, &a);
    ICUNIT_GOTO_EQUAL(ret, -4, ret, EXIT);

    /* port 越界 → -4 */
    ret = uart_init(10, &a);
    ICUNIT_GOTO_EQUAL(ret, -4, ret, EXIT);

    /* 对照: 正常 init → 0 */
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 重复 init → -2 */
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, -2, ret, EXIT);

    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_OK;
EXIT:
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart001(VOID)
{
    TEST_ADD_CASE("ItDriverUart001", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL1, TEST_FUNCTION);
}
