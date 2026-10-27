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
 * ItLosUart003 L0: MCR self-loopback data roundtrip (no wiring)
 */

#include "It_los_uart.h"
#include "iCunit.h"
#include "osTest.h"

static UINT32 UartCase003(VOID)
{
    UINT32 ret;
    UINT32 i;
    UINT32 got;
    uart_attr_t attr = UartTestAttrDefault();
    UINT8 txbuf[UART_TEST_LEN];
    UINT8 rxbuf[UART_TEST_LEN];

    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);

    UartTestMcrLoopOn();

    for (i = 0; i < UART_TEST_LEN; i++) {
        txbuf[i] = (UINT8)(i + 1);
    }

    ret = (UINT32)uart_write(UART_TEST_PORT, txbuf, UART_TEST_LEN, 200);
    if (ret != (UINT32)UART_TEST_LEN) {
        UartTestMcrLoopOff();
        (VOID)uart_deinit(UART_TEST_PORT);
        ICUNIT_ASSERT_EQUAL(ret, UART_TEST_LEN, ret);
        return LOS_OK;
    }

    got = 0;
    while (got < UART_TEST_LEN) {
        ret = (UINT32)uart_read(UART_TEST_PORT, rxbuf + got,
                                (UINT16)(UART_TEST_LEN - got), 200);
        if (ret <= 0) {
            break;
        }
        got += (UINT32)ret;
    }

    UartTestMcrLoopOff();
    (VOID)uart_deinit(UART_TEST_PORT);

    ICUNIT_ASSERT_EQUAL(got, UART_TEST_LEN, got);
    for (i = 0; i < UART_TEST_LEN; i++) {
        if (rxbuf[i] != txbuf[i]) {
            ICUNIT_ASSERT_EQUAL(rxbuf[i], txbuf[i], i);
            break;
        }
    }

    return LOS_OK;
}

VOID ItLosUart003(VOID)
{
    TEST_ADD_CASE("ItLosUart003", UartCase003, TEST_LOS, TEST_UART_DRV, TEST_LEVEL0, TEST_FUNCTION);
}
