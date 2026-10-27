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
 * ItLosUart002 L0: API error paths (no hardware I/O)
 */

#include "It_los_uart.h"
#include "iCunit.h"
#include "osTest.h"

static UINT32 UartCase002(VOID)
{
    UINT32 ret;
    uart_attr_t attr = UartTestAttrDefault();
    UINT8 buf[4];

    /* invalid port numbers */
    ret = (UINT32)uart_init(0, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_NOT_SUP, ret);
    ret = (UINT32)uart_init(10, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_NOT_SUP, ret);
    ret = (UINT32)uart_deinit(0);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_NOT_SUP, ret);

    /* NULL attr */
    ret = (UINT32)uart_init(UART_TEST_PORT, NULL);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_PARAM, ret);

    /* invalid baud */
    attr.baudrate = 100;
    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_PARAM, ret);

    /* invalid data_bits */
    attr = UartTestAttrDefault();
    attr.data_bits = 9;
    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_PARAM, ret);

    /* invalid stop_bits */
    attr = UartTestAttrDefault();
    attr.stop_bits = 3;
    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_PARAM, ret);

    /* illegal combo: parity needs 1 stop bit (E-2 is a 12-bit frame) */
    attr = UartTestAttrDefault();
    attr.parity = UART_PARITY_EVEN;
    attr.stop_bits = 2;
    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_PARAM, ret);

    /* negative timeout not supported */
    attr = UartTestAttrDefault();
    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);
    ret = (UINT32)uart_write(UART_TEST_PORT, buf, 1, -1);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_NOT_SUP, ret);
    ret = (UINT32)uart_read(UART_TEST_PORT, buf, 1, -1);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_NOT_SUP, ret);

    /* NULL buffer on inited port */
    ret = (UINT32)uart_write(UART_TEST_PORT, NULL, 1, 100);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_PARAM, ret);
    ret = (UINT32)uart_read(UART_TEST_PORT, NULL, 1, 100);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_PARAM, ret);

    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_OK;
}

VOID ItLosUart002(VOID)
{
    TEST_ADD_CASE("ItLosUart002", UartCase002, TEST_LOS, TEST_UART_DRV, TEST_LEVEL0, TEST_FUNCTION);
}
