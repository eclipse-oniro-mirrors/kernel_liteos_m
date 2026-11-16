/* 用例简要描述: ItDriverUart203 运行时改线制（set_attr 切速/切校验后回环仍全等） */
#include "it_driver_uart.h"
#include "uart_drv.h"
#include "uart_soc_rk3568.h"

#define UART_TEST_PORT 1
#define UART1_MCR ((volatile uint32_t *)(RK3568_UART1_BASE + 0x10))

static UINT32 Round32(void)
{
    static uint8_t txbuf[32];
    static uint8_t rxbuf[32];
    int32_t ret, i;

    for (i = 0; i < 32; i++) {
        txbuf[i] = (uint8_t)(0x30 + i);
    }
    ret = uart_write(UART_TEST_PORT, txbuf, 32, 5000);
    if (ret != 32) {
        return LOS_NOK;
    }
    {
        int32_t total = 0;

        while (total < 32) {
            ret = uart_read(UART_TEST_PORT, rxbuf + total, (uint16_t)(32 - total), 2000);
            if (ret <= 0) {
                break;
            }
            total += ret;
        }
        if (total != 32 || memcmp(rxbuf, txbuf, 32) != 0) {
            return LOS_NOK;
        }
    }
    return LOS_OK;
}

static UINT32 Testcase(VOID)
{
    uart_attr_t a;
    int32_t ret;

    a.baudrate = 115200;
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN; /* 合法线制 E-1（N 必须 2 停止位） */
    a.stop_bits = 1;

    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    *UART1_MCR |= 0x10;

    /* 基线回环 */
    if (Round32() != LOS_OK) {
        ICUNIT_GOTO_EQUAL(1, 0, 1, EXIT);
    }

    /* 改速 9600 → 回环 */
    ret = uart_tx_flush(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = uart_set_attr(UART_TEST_PORT, 9600, UART_PARITY_EVEN, 1);
    dprintf("[UartScn03] set_attr EVEN ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    if (Round32() != LOS_OK) {
        ICUNIT_GOTO_EQUAL(1, 0, 2, EXIT);
    }

    /* 改校验 ODD+1 停止位（合法组合 E-1/O-1/N-2；E-2 非法由 API 族覆盖） → 回环 */
    ret = uart_tx_flush(UART_TEST_PORT);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = uart_set_attr(UART_TEST_PORT, 9600, UART_PARITY_ODD, 1);
    dprintf("[UartScn03] set_attr ODD ret=%d\n", ret);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    if (Round32() != LOS_OK) {
        ICUNIT_GOTO_EQUAL(1, 0, 3, EXIT);
    }

    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    dprintf("[UartScn03] reline: 115200→9600→EVEN/2s all-equal\n");
    return LOS_OK;
EXIT:
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart203(VOID)
{
    TEST_ADD_CASE("ItDriverUart203", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL1, TEST_FUNCTION);
}
