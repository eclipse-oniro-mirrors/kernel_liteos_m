/* 用例简要描述: ItDriverUart202 波特率矩阵（1200/9600/115200 三档内环回环 32B 全等） */
#include "it_driver_uart.h"
#include "uart_drv.h"
#include "uart_soc_rk3568.h"

#define UART_TEST_PORT 1
#define UART1_MCR ((volatile uint32_t *)(RK3568_UART1_BASE + 0x10))

static UINT32 OneBaudRound(uint32_t baud)
{
    uart_attr_t a;
    static uint8_t txbuf[32];
    static uint8_t rxbuf[32];
    int32_t ret, i;

    a.baudrate = baud;
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN; /* 合法线制 E-1（N 必须 2 停止位） */
    a.stop_bits = 1;

    for (i = 0; i < 32; i++) {
        txbuf[i] = (uint8_t)(baud + i);
    }

    ret = uart_init(UART_TEST_PORT, &a);
    if (ret != 0) {
        return LOS_NOK;
    }
    ret = uart_write(UART_TEST_PORT, txbuf, 32, 5000);
    if (ret != 32) {
        (VOID)uart_deinit(UART_TEST_PORT);
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
            (VOID)uart_deinit(UART_TEST_PORT);
            return LOS_NOK;
        }
    }
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_OK;
}

static UINT32 Testcase(VOID)
{
    static const uint32_t bauds[] = {1200, 9600, 115200};
    uint32_t i;

    *UART1_MCR |= 0x10;
    for (i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
        dprintf("[UartScn02] baud=%u round...\n", bauds[i]);
        if (OneBaudRound(bauds[i]) != LOS_OK) {
            *UART1_MCR &= ~0x10U;
            ICUNIT_GOTO_EQUAL(1, 0, (int)bauds[i], EXIT);
        }
    }
    *UART1_MCR &= ~0x10U;
    dprintf("[UartScn02] baud matrix 3/3 all-equal\n");
    return LOS_OK;
EXIT:
    *UART1_MCR &= ~0x10U;
    return LOS_NOK;
}

VOID ItDriverUart202(VOID)
{
    TEST_ADD_CASE("ItDriverUart202", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL1, TEST_FUNCTION);
}
