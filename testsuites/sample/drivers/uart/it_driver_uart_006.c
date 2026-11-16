/* 用例简要描述: ItDriverUart006 uart_read 超时部分读语义（短超时返回已读数） */
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
    int32_t ret, i;

    a.baudrate = 1200; /* 慢速制造在途 */
    a.data_bits = 8;
    a.parity = UART_PARITY_EVEN; /* 合法线制 E-1（N 必须 2 停止位） */
    a.stop_bits = 1;

    ret = uart_init(UART_TEST_PORT, &a);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    for (i = 0; i < 64; i++) {
        txbuf[i] = (uint8_t)(i * 3);
    }

    /* MCR 内环 */
    *UART1_MCR |= 0x10;

    /* write(timeout=0) 仅接纳不排空——TX 在环里排空中（64B@1200≈0.53s），
     * 100ms 短超时读捕获排空中途的部分数据（write 阻塞版会等到 TEMT 全到） */
    ret = uart_write(UART_TEST_PORT, txbuf, 64, 0);
    ICUNIT_GOTO_EQUAL(ret, 64, ret, EXIT);
    ret = uart_read(UART_TEST_PORT, rxbuf, 64, 100);
    dprintf("[ApiUartRw02] write(0)+read(100ms) ret=%d (loop-delay probe)\n", ret);
    ICUNIT_GOTO_NOT_EQUAL(ret, 0, ret, EXIT); /* 至少读到一些 */
    ICUNIT_GOTO_EQUAL((ret < 64), 1, ret, EXIT); /* 排空中途（1200 波特 100ms < 0.53s） */

    /* 补读至读满（长超时） */
    {
        int32_t total = ret;
        int32_t remain = 64 - total;

        while (remain > 0) {
            ret = uart_read(UART_TEST_PORT, rxbuf + total, (uint16_t)remain, 2000);
            if (ret <= 0) {
                break;
            }
            total += ret;
            remain -= ret;
        }
        dprintf("[ApiUartRw02] total=%d\n", total);
        ICUNIT_GOTO_EQUAL(total, 64, total, EXIT);
        if (memcmp(rxbuf, txbuf, 64) != 0) {
            ICUNIT_GOTO_EQUAL(1, 0, -1, EXIT);
        }
    }

    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_OK;
EXIT:
    *UART1_MCR &= ~0x10U;
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_NOK;
}

VOID ItDriverUart006(VOID)
{
    TEST_ADD_CASE("ItDriverUart006", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_UART, TEST_LEVEL1, TEST_FUNCTION);
}
