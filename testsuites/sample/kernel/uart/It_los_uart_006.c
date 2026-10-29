/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * ItLosUart006 L0: hardware-range validation (post Modbus-RTU widening):
 * baud-range boundaries (23 lowest, 1500000 highest, 300 low band) and
 * newly legal line combos (E-2 / O-2 / N-1), MCR self-loopback.
 */

#include "It_los_uart.h"
#include "iCunit.h"
#include "osTest.h"

#define UART006_LEN 16U

/* one full cycle: init -> MCR loop -> roundtrip -> compare -> deinit;
 * returns UART_OK, or the first nonzero error/gap value seen. */
static UINT32 Uart006Roundtrip(UINT32 baudrate, uart_parity_t parity, UINT8 stopBits)
{
    uart_attr_t attr = UartTestAttrDefault();
    UINT8 txbuf[UART006_LEN];
    UINT8 rxbuf[UART006_LEN];
    UINT32 tmo = (UART006_LEN * 10U * 4000U) / baudrate + 100U;
    UINT32 ret;
    UINT32 got;
    UINT32 i;

    attr.baudrate = baudrate;
    attr.parity = parity;
    attr.stop_bits = stopBits;

    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    if (ret != UART_OK) {
        return ret;
    }

    UartTestMcrLoopOn();
    for (i = 0; i < UART006_LEN; i++) {
        txbuf[i] = (UINT8)(i + 0xA5U);
    }

    ret = (UINT32)uart_write(UART_TEST_PORT, txbuf, UART006_LEN, (INT32)tmo);
    if (ret != UART006_LEN) {
        UartTestMcrLoopOff();
        (VOID)uart_deinit(UART_TEST_PORT);
        return (ret != 0U) ? ret : (UINT32)UART_ERR_BUSY;
    }

    got = 0;
    while (got < UART006_LEN) {
        ret = (UINT32)uart_read(UART_TEST_PORT, rxbuf + got,
                                (UINT16)(UART006_LEN - got), (INT32)tmo);
        if (ret == 0U) {
            break;
        }
        got += ret;
    }

    UartTestMcrLoopOff();
    (VOID)uart_deinit(UART_TEST_PORT);

    if (got != UART006_LEN) {
        return (UINT32)UART_ERR_PARAM;
    }
    for (i = 0; i < UART006_LEN; i++) {
        if (rxbuf[i] != txbuf[i]) {
            return (UINT32)UART_ERR_PARAM;
        }
    }
    return UART_OK;
}

static UINT32 UartCase006(VOID)
{
    UINT32 ret;
    uart_attr_t attr = UartTestAttrDefault();

    /* lowest baud (divisor 65222) must be accepted; no traffic - too slow */
    attr.baudrate = 23;
    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);
    ret = (UINT32)uart_deinit(UART_TEST_PORT);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);

    /* highest baud: divisor 1, exact 1.5Mbps roundtrip */
    ret = Uart006Roundtrip(1500000, UART_PARITY_EVEN, 1);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);

    /* low band below the old 1200 floor: 300bps roundtrip */
    ret = Uart006Roundtrip(300, UART_PARITY_EVEN, 1);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);

    /* newly legal combos (previously Modbus-RTU 11-bit restricted) */
    ret = Uart006Roundtrip(115200, UART_PARITY_EVEN, 2);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);
    ret = Uart006Roundtrip(115200, UART_PARITY_ODD, 2);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);
    ret = Uart006Roundtrip(115200, UART_PARITY_NONE, 1);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);

    return LOS_OK;
}

VOID ItLosUart006(VOID)
{
    TEST_ADD_CASE("ItLosUart006", UartCase006, TEST_LOS, TEST_UART_DRV, TEST_LEVEL0, TEST_FUNCTION);
}
