/* 用例简要描述: ItDriverUart301 MCR 内环 100 轮×64B@115200 持续收发（全等校验） */
#include "it_driver_uart.h"
#include "uart_drv.h"
#include "uart_soc_rk3568.h"

#define UART_TEST_PORT 1
#define UART1_MCR ((volatile uint32_t *)(RK3568_UART1_BASE + 0x10))

static UINT32 Testcase(VOID)
{
    uart_attr_t a;
    static uint8_t txbuf[64];
    static uint8_t rxbuf[64];
    uint32_t i, j;
    int32_t ret, fails = 0;

    a.baudrate = 115200;
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN;
    a.stop_bits = 1;
    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    *UART1_MCR |= 0x10;
    for (i = 0; i < 100; i++) {
        for (j = 0; j < 64; j++) {
            txbuf[j] = (uint8_t)(i + j);
        }
        ret = uart_write(UART_TEST_PORT, txbuf, 64, 3000);
        if (ret != 64) {
            dprintf("[StressUart01] #%u write=%d\n", i, ret);
            fails++;
            continue;
        }
        {
            int32_t total = 0;

            while (total < 64) {
                ret = uart_read(UART_TEST_PORT, rxbuf + total, (uint16_t)(64 - total), 3000);
                if (ret <= 0) {
                    break;
                }
                total += ret;
            }
            if (total != 64 || memcmp(rxbuf, txbuf, 64) != 0) {
                dprintf("[StressUart01] #%u total=%d\n", i, total);
                fails++;
            }
        }
        if (fails > 3) {
            break;
        }
    }
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    dprintf("[StressUart01] rounds=%u fails=%d\n", i, fails);
    ICUNIT_GOTO_EQUAL(fails, 0, fails, EXIT);
    return LOS_OK;
EXIT:
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart301(VOID)
{
    TEST_ADD_CASE("ItDriverUart301", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL1, TEST_FUNCTION);
}
