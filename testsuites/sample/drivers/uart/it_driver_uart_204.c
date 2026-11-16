/* 用例简要描述: ItDriverUart204 set_attr BUSY 路径（TX 在途→-3→flush 后→0） */
#include "it_driver_uart.h"
#include "uart_drv.h"

#define UART_TEST_PORT 1

static UINT32 Testcase(VOID)
{
    uart_attr_t a;
    static uint8_t buf[256];
    int32_t ret;

    a.baudrate = 1200; /* 256B@1200 ≈ 2.1s 在途 */
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN; /* 合法线制 E-1（N 必须 2 停止位） */
    a.stop_bits = 1;

    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 发起长写（不等待完成——write 自身阻塞至完成？设计书：timeout 预算覆盖全流程。
     * 用 timeout=0 写（仅接纳不排空）——TX 在 ring 里排空中，立即 set_attr 应 BUSY */
    ret = uart_write(UART_TEST_PORT, buf, 256, 0);
    dprintf("[UartScn04] write(timeout=0) ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL(ret, 256, ret, EXIT); /* 接纳成功 */

    /* 立即 set_attr → -3 BUSY（256B@1200 仍在途） */
    ret = uart_set_attr(UART_TEST_PORT, 9600, UART_PARITY_EVEN, 1);
    dprintf("[UartScn04] set_attr during TX ret=%d (expect -3)\n", ret);
    ICUNIT_GOTO_EQUAL(ret, -3, ret, EXIT);

    /* flush 排空后 → 0 */
    ret = uart_tx_flush(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = uart_set_attr(UART_TEST_PORT, 9600, UART_PARITY_EVEN, 1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_OK;
EXIT:
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart204(VOID)
{
    TEST_ADD_CASE("ItDriverUart204", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL1, TEST_FUNCTION);
}
