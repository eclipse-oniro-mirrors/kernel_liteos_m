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
 * ItLosUart005 L0: init/deinit/re-init idempotency cycle
 */

#include "It_los_uart.h"
#include "iCunit.h"
#include "osTest.h"

static UINT32 UartCase005(VOID)
{
    UINT32 ret;
    UINT32 cycle;
    uart_attr_t attr = UartTestAttrDefault();

    for (cycle = 0; cycle < 3; cycle++) {
        ret = (UINT32)uart_init(UART_TEST_PORT, &attr);
        ICUNIT_GOTO_EQUAL(ret, UART_OK, ret, EXIT);
        ret = (UINT32)uart_deinit(UART_TEST_PORT);
        ICUNIT_GOTO_EQUAL(ret, UART_OK, ret, EXIT);
    }

    /* deinit on already-deinited port: idempotent */
    ret = (UINT32)uart_deinit(UART_TEST_PORT);
    ICUNIT_ASSERT_EQUAL(ret, UART_OK, ret);

    return LOS_OK;
EXIT:
    (VOID)uart_deinit(UART_TEST_PORT);
    return LOS_OK;
}

VOID ItLosUart005(VOID)
{
    TEST_ADD_CASE("ItLosUart005", UartCase005, TEST_LOS, TEST_UART_DRV, TEST_LEVEL0, TEST_FUNCTION);
}
