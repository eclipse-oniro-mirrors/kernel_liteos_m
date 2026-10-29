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
 * rk3568_m UART driver ICUnit integration tests (LEVEL0): wraps the board
 * UART driver public API into ICUnit cases.  Test port is UART3; the
 * console (UART2, master-shared) is never touched.
 */

#ifndef _IT_LOS_UART_H
#define _IT_LOS_UART_H

#include "los_typedef.h"
#include "los_toolchain.h"
#include "uart_drv.h"
#include "uart_board_cfg.h"

#define UART_TEST_PORT  3  /* product-convention UART3 (M1, 20-pin J9903) */
#define UART_TEST_LEN  64

/* MCR internal loopback: MCR.LOOP (bit 4, 16550) written directly - the
 * driver's loop helper is STATIC, so the register write is done here. */
#define UART3_BASE 0xFE670000UL
#define UART_REG_MCR 0x10
#define UART_MCR_LOOP 0x10

STATIC INLINE VOID UartTestMcrLoopOn(VOID)
{
    volatile UINT32 *mcr = (volatile UINT32 *)(UART3_BASE + UART_REG_MCR);
    *mcr |= UART_MCR_LOOP;
}

STATIC INLINE VOID UartTestMcrLoopOff(VOID)
{
    volatile UINT32 *mcr = (volatile UINT32 *)(UART3_BASE + UART_REG_MCR);
    *mcr &= ~UART_MCR_LOOP;
}

/* standard 8-E-1 line attribute */
STATIC INLINE uart_attr_t UartTestAttrDefault(VOID)
{
    uart_attr_t attr = {
        .baudrate = 115200,
        .data_bits = 8,
        .parity = UART_PARITY_EVEN,
        .stop_bits = 1,
    };
    return attr;
}

VOID ItLosUart001(VOID);
VOID ItLosUart002(VOID);
VOID ItLosUart003(VOID);
VOID ItLosUart004(VOID);
VOID ItLosUart005(VOID);
VOID ItLosUart006(VOID);

#endif /* _IT_LOS_UART_H */
