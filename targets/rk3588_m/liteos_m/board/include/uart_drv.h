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
 *
 * RK3588 UART driver interface (Modbus-RTU oriented, protocol agnostic).
 *
 * Contract source: 《驱动接口依赖.md》ModBus-RTU chapter (7 P0 APIs) as
 * adopted by 《Modbus-RTU_UART驱动实现方案_rk3588.md》V0.17:
 *  - legal line combos: E-1 / O-1 / N-2 (Q5), data_bits 5~8;
 *  - write() timeout covers the whole flow (accept + drain, TEMT anchored),
 *    partial return is legal and TX is flushed on timeout;
 *  - error codes are exactly the five below (V0.7 principle: no invented
 *    codes, no semantic reuse).
 *
 * The header only uses standard C types so the protocol layer (KAL /
 * nanoMODBUS port) can include it without LiteOS headers.
 */

#ifndef UART_DRV_H
#define UART_DRV_H

#include <stdint.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* Error codes (authoritative set, do not extend without a doc revision). */
#define UART_OK          0     /* read/write: actual byte count, may be 0 */
#define UART_ERR_PARAM   (-1)  /* NULL pointer / field out of range / bad combo */
#define UART_ERR_STATE   (-2)  /* not initialized, or duplicate init */
#define UART_ERR_BUSY    (-3)  /* device busy (e.g. set_attr during TX) */
#define UART_ERR_NOT_SUP (-4)  /* capability/port not supported */

typedef enum {
    UART_PARITY_NONE = 0, /* no parity (Modbus RTU requires 2 stop bits) */
    UART_PARITY_ODD  = 1,
    UART_PARITY_EVEN = 2, /* Modbus RTU default */
} uart_parity_t;

typedef struct {
    uint32_t      baudrate;  /* 1200 ~ 115200 */
    uint8_t       data_bits; /* 5 ~ 8; Modbus RTU uses 8 */
    uart_parity_t parity;
    uint8_t       stop_bits; /* 1 or 2; legal combos: E-1 / O-1 / N-2 */
} uart_attr_t;

/*
 * Initialize a UART port (clock + pinmux + GIC routing + 16550 registers)
 * with the given attributes. The attr structure is only read during the
 * call (the driver keeps no pointer to it).
 * Caller context: task (after the scheduler is up), one init at a time.
 */
int32_t uart_init(uint8_t port, const uart_attr_t *attr);

/* De-initialize: mask IRQ, release semaphores, reset state. Clock and
 * pinmux are left configured (re-init is cheaper and idempotent). */
int32_t uart_deinit(uint8_t port);

/*
 * Blocking read with timeout.
 *  timeout_ms > 0 : block up to that long (return on full count or timeout)
 *  timeout_ms == 0: non-blocking poll
 *  timeout_ms < 0 : not supported (returns UART_ERR_NOT_SUP)
 * Returns the number of bytes actually read (0 ~ count).
 * Single-reader model: only one task may call uart_read per port.
 */
int32_t uart_read(uint8_t port, uint8_t *buf, uint16_t count, int32_t timeout_ms);

/*
 * Blocking write with timeout. The timeout budget covers the WHOLE flow:
 * acceptance (FIFO/ring) plus the drain phase (TX ring empty AND LSR TEMT,
 * i.e. every bit on the wire), so on return the frame has fully left the
 * shifter - the protocol layer anchors t3.5 on the write return.
 * Returns the number of bytes fully transmitted to the bus:
 *  - normal path (budget sufficient): count
 *  - timeout: partial count (TX FIFO level aware), and the driver flushes
 *    the remaining TX (residual-frame semantics, prevents late frames)
 *  - timeout_ms == 0: accept-only (no drain); caller must uart_tx_flush
 *  - timeout_ms < 0 : not supported (returns UART_ERR_NOT_SUP)
 * One-frame-one-call constraint: splitting a frame into multiple write()
 * calls may break the intra-frame t1.5 gap (scheduler latency between
 * calls) - the protocol layer must not do it.
 */
int32_t uart_write(uint8_t port, const uint8_t *buf, uint16_t count, int32_t timeout_ms);

/*
 * Block until the last byte has completely left the transmit shift
 * register (LSR TEMT). No timeout by contract (Q7: the interface doc
 * defines no timeout error code; BUSY must not be reused).
 */
int32_t uart_tx_flush(uint8_t port);

/*
 * Reconfigure baudrate / parity / stop bits at runtime (data_bits stays
 * as configured at init). Completes in <= 1 ms (register level only).
 * Returns UART_ERR_BUSY while a transmission is in flight.
 */
int32_t uart_set_attr(uint8_t port, uint32_t baudrate, uart_parity_t parity, uint8_t stop_bits);

/* Discard all received (buffered) data. */
int32_t uart_rx_flush(uint8_t port);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* UART_DRV_H */
