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
 * ItLosUart004 L0: write timeout=0 (accept-only) + tx_flush
 */

#include "It_los_uart.h"
#include "iCunit.h"
#include "osTest.h"

static UINT32 UartCase004(VOID)
{
    UINT32 ret;
    uart_attr_t attr = UartTestAttrDefault();
    UINT8 buf[UART_TEST_LEN];

    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);

    UartTestMcrLoopOn();

    /* timeout=0: accept-only */
    ret = (UINT32)uart_write(UART_TEST_PORT, buf, UART_TEST_LEN, 0);
    if (ret != (UINT32)UART_TEST_LEN) {
        UartTestMcrLoopOff();
        (VOID)uart_deinit(UART_TEST_PORT);
        ICUNIT_ASSERT_EQUAL(ret, UART_TEST_LEN, ret);
        return LOS_OK;
    }

    /* tx_flush blocks until wire empty */
    ret = (UINT32)uart_tx_flush(UART_TEST_PORT);
    UartTestMcrLoopOff();
    (VOID)uart_deinit(UART_TEST_PORT);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);

    return LOS_OK;
}

VOID ItLosUart004(VOID)
{
    TEST_ADD_CASE("ItLosUart004", UartCase004, TEST_LOS, TEST_UART_DRV, TEST_LEVEL0, TEST_FUNCTION);
}
