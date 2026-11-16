/* 用例简要描述: ItDriverUart201 MCR 内环回环（UART1@115200 256B 收发 memcmp 全等） */
#include "it_driver_uart.h"
#include "uart_drv.h"
#include "uart_soc_rk3568.h"

#define UART_TEST_PORT 1
#define UART1_MCR ((volatile uint32_t *)(RK3568_UART1_BASE + 0x10))

static UINT32 Testcase(VOID)
{
    uart_attr_t a;
    static uint8_t txbuf[256];
    static uint8_t rxbuf[256];
    int32_t ret, i;

    a.baudrate = 115200;
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN; /* 合法线制 E-1（N 必须 2 停止位） */
    a.stop_bits = 1;

    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    for (i = 0; i < 256; i++) {
        txbuf[i] = (uint8_t)(i ^ 0xA5);
    }

    *UART1_MCR |= 0x10; /* MCR 内环开 */

    ret = uart_write(UART_TEST_PORT, txbuf, 256, 5000);
    ICUNIT_GOTO_EQUAL(ret, 256, ret, EXIT);
    {
        int32_t total = 0;

        while (total < 256) {
            ret = uart_read(UART_TEST_PORT, rxbuf + total, (uint16_t)(256 - total), 2000);
            if (ret <= 0) {
                break;
            }
            total += ret;
        }
        ICUNIT_GOTO_EQUAL(total, 256, total, EXIT);
        if (memcmp(rxbuf, txbuf, 256) != 0) {
            ICUNIT_GOTO_EQUAL(1, 0, -1, EXIT);
        }
    }

    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    dprintf("[UartScn01] MCR loopback 256B @115200 all-equal\n");
    return LOS_OK;
EXIT:
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart201(VOID)
{
    TEST_ADD_CASE("ItDriverUart201", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL0, TEST_FUNCTION);
}
