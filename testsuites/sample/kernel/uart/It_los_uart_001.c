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
 * ItLosUart001 L0: init/deinit lifecycle + state transitions
 */

#include "It_los_uart.h"
#include "iCunit.h"
#include "osTest.h"

static UINT32 UartCase001(VOID)
{
    UINT32 ret;
    uart_attr_t attr = UartTestAttrDefault();
    UINT8 buf[4];

    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);

    /* duplicate init must fail */
    ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_STATE, ret);

    ret = (UINT32)uart_deinit(UART_TEST_PORT);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);

    /* write/read on a deinited port must fail */
    ret = (UINT32)uart_write(UART_TEST_PORT, buf, 1, 100);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_STATE, ret);
    ret = (UINT32)uart_read(UART_TEST_PORT, buf, 1, 100);
    ICUNIT_ASSERT_EQUAL(ret, UART_ERR_STATE, ret);

    return LOS_OK;
}

VOID ItLosUart001(VOID)
{
    TEST_ADD_CASE("ItLosUart001", UartCase001, TEST_LOS, TEST_UART_DRV, TEST_LEVEL0, TEST_FUNCTION);
}
