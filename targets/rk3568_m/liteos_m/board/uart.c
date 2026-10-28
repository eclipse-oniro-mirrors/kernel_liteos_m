/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * RK3568 AMP UART driver (DW 8250 / 16550-compatible, interrupt mode).
 *
 * Implements the 7-API Modbus-RTU oriented contract in uart_drv.h:
 * interrupt-driven RX/TX with per-port ring buffers, semaphore blocking
 * semantics, line-synchronous write (LSR TEMT anchored) and a static
 * UART1~UART9 board profile (CRU clock / GRF pinmux / GIC SPI routing).
 *
 * Design source: 《Modbus-RTU_UART驱动实现方案_rk3568.md》V0.17:
 *  - ports 1..9 supported ("pass an id, get it initialized"; UART0 and
 *    out-of-range ids return UART_ERR_NOT_SUP - technical gap, not policy);
 *  - UART2 is shared with the Linux master console (ttyFIQ0): this driver
 *    initializes it like any other port when asked to - the master console
 *    migration is the caller's responsibility (§4.2 warning);
 *  - DW 8250 quirks honored: 32-bit register access (reg-shift=2), 32-byte
 *    FIFOs, RX timeout interrupt (IIR 0x0C) drains below the FIFO trigger,
 *    TFL (0x80) gives the exact TX FIFO level for partial-write accounting;
 *  - IER is kept as a shadow copy (avoids DLAB confusion, atomic on/off);
 *  - ISR dispatches by IIR in a bounded loop (anti-livelock, NuttX pattern);
 *  - RX ring full -> drop bytes and count (rx_dropped; Modbus resyncs via
 *    CRC + t3.5); error bytes (OE/PE/FE/BI) are dropped early and counted;
 *  - TX: accepted bytes go to the FIFO (fast path) then the ring, the ISR
 *    refills from the ring under ETBEI and disables ETBEI when drained;
 *  - write(timeout>0) covers accept + drain with one budget and returns
 *    the number of bytes fully on the wire; on timeout the TX is flushed
 *    (FIFO reset + ring drop + ETBEI off) so no late frame can collide
 *    with a master retry (residual-frame semantics);
 *  - RS485 direction control is out of scope (auto-direction transceiver,
 *    Q11/V0.17); the optional "drop RX while TX active" echo suppression
 *    of such transceivers is a per-port profile flag (default off).
 *
 * Concurrency model (§4.5): per-port data paths share nothing across
 * ports; ISR vs task uses LOS_IntLock short critical sections; a single
 * global mutex serializes init/deinit only (GICD read-modify-write regs);
 * one reader + one writer task per port is the supported model.
 */

#include "uart_drv.h"

#ifdef LOSCFG_RK3568_UART

#include "los_typedef.h"
#include "los_sem.h"
#include "los_mux.h"
#include "los_task.h"
#include "los_tick.h"
#include "los_interrupt.h"
#include "los_debug.h"
#include "los_hwi.h"
#include "securec.h"
#include "uart_board_cfg.h"

/* ===========================================================================
 * 16550 / DW APB UART registers (byte offsets, 32-bit access, reg-shift=2).
 * ========================================================================= */
#define UART_REG_RBR        0x00    /* RX buffer / DLL (DLAB=1) */
#define UART_REG_THR        0x00    /* TX buffer / DLL (DLAB=1) */
#define UART_REG_IER        0x04    /* int enable / DLM (DLAB=1) */
#define UART_REG_IIR        0x08    /* int identification (read) */
#define UART_REG_FCR        0x08    /* FIFO control (write) */
#define UART_REG_LCR        0x0C    /* line control */
#define UART_REG_MCR        0x10    /* modem control (untouched by default, Q11) */
#define UART_REG_LSR        0x14    /* line status */
#define UART_REG_TFL        0x80    /* DW: TX FIFO level */

/* IER bits */
#define UART_IER_ERBFI      0x01U
#define UART_IER_ETBEI      0x02U
#define UART_IER_ELSI       0x04U

/* IIR bits */
#define UART_IIR_NOT_PENDING    0x01U
#define UART_IIR_ID_MASK        0x0FU
#define UART_IIR_MS             0x00U
#define UART_IIR_THRE           0x02U
#define UART_IIR_RDA            0x04U
#define UART_IIR_RLS            0x06U
#define UART_IIR_CTI            0x0CU  /* RX timeout: drain below trigger level */

/* FCR bits (RX trigger 1/8 = 4 of 32 bytes) */
#define UART_FCR_FIFO_EN    0x01U
#define UART_FCR_RX_RST     0x02U
#define UART_FCR_TX_RST     0x04U
#define UART_FCR_RX_TRIG_1_8    0x00U

/* LCR bits */
#define UART_LCR_STB        0x04U
#define UART_LCR_PEN        0x08U
#define UART_LCR_EPS        0x10U
#define UART_LCR_DLAB       0x80U

/* LSR bits */
#define UART_LSR_DR         0x01U
#define UART_LSR_OE         0x02U
#define UART_LSR_PE         0x04U
#define UART_LSR_FE         0x08U
#define UART_LSR_BI         0x10U
#define UART_LSR_THRE       0x20U
#define UART_LSR_TEMT       0x40U
#define UART_LSR_ANY_ERR    (UART_LSR_OE | UART_LSR_PE | UART_LSR_FE | UART_LSR_BI)

#define UART_TX_FIFO_DEPTH  32U     /* DW 8250 on RK3568 */

/* ===========================================================================
 * Port profile types. The concrete per-port data (MMIO base, GIC id,
 * clock registers, pinmux group) is generated from uart_board_cfg.h
 * (board wiring picks) on top of uart_soc_rk3568.h (SoC facts, verified
 * against mainline rk356x.dtsi + clk-rk3568.c + rk3568-pinctrl.dtsi +
 * on-board measurements, appendix B). New boards adapt the header only,
 * never this file.
 * ========================================================================= */
typedef struct {
    UINT32 reg;      /* GRF iomux register, 0 = entry unused */
    UINT32 val;      /* HIWORD16 write value */
} UartPinmuxWrite;

typedef struct {
    UINT32 reg;      /* CRU softrst register, 0 = no reset */
    UINT32 mask;     /* HIWORD16 bit mask for this UART's reset lines */
} UartSoftRst;

typedef struct {
    UINT8 portNr;             /* logical port id (1..9) */
    UINT32 base;              /* UART MMIO base */
    UINT16 intId;             /* GIC INTID (SPI + 32) */
    UINT32 clkselReg;         /* CRU CLKSEL_CON: parent mux -> xin24m */
    UINT32 clkselVal;         /* HIWORD16 write (mask 0x3000, data 0x2000) */
    UINT32 gateReg;           /* CRU CLKGATE_CON: sclk+pclk gates */
    UINT32 gateVal;           /* HIWORD16 write (0 = enable, SET_TO_DISABLE) */
    UartPinmuxWrite pinmux[4];/* iomux + optional pull writes, see uart_soc_rk3568.h */
    UartSoftRst softrst;      /* CRU SOFTRST: assert+de-assert to init controller */
    BOOL rxEchoSuppress;      /* RS485 auto-direction transceiver echo drop */
} UartPortProfile;

/* ===========================================================================
 * Port profiles, generated from uart_board_cfg.h. Ports disabled by the
 * board config simply have no object here; their g_uartCtx slot keeps
 * profile == NULL and every API returns UART_ERR_NOT_SUP for them.
 * ========================================================================= */
#if BOARD_UART1_EN
STATIC const UartPortProfile g_uart1Profile = {
    .portNr = 1,
    .base = RK3568_UART1_BASE,
    .intId = RK3568_UART1_INT_ID,
    .clkselReg = RK3568_UART1_CLKSEL_REG,
    .clkselVal = RK3568_UART_CLKSEL_VAL_XIN24M,
    .gateReg = RK3568_UART1_GATE_REG,
    .gateVal = RK3568_UART1_GATE_VAL,
    .pinmux = BOARD_UART1_PINMUX,
    .rxEchoSuppress = BOARD_UART1_RS485_ECHO,
};
#endif

#if BOARD_UART2_EN
STATIC const UartPortProfile g_uart2Profile = {
    .portNr = 2,
    .base = RK3568_UART2_BASE,
    .intId = RK3568_UART2_INT_ID,
    .clkselReg = RK3568_UART2_CLKSEL_REG,
    .clkselVal = RK3568_UART_CLKSEL_VAL_XIN24M,
    .gateReg = RK3568_UART2_GATE_REG,
    .gateVal = RK3568_UART2_GATE_VAL,
    .pinmux = BOARD_UART2_PINMUX,
    .rxEchoSuppress = BOARD_UART2_RS485_ECHO,
};
#endif

#if BOARD_UART3_EN
STATIC const UartPortProfile g_uart3Profile = {
    .portNr = 3,
    .base = RK3568_UART3_BASE,
    .intId = RK3568_UART3_INT_ID,
    .clkselReg = RK3568_UART3_CLKSEL_REG,
    .clkselVal = RK3568_UART_CLKSEL_VAL_XIN24M,
    .gateReg = RK3568_UART3_GATE_REG,
    .gateVal = RK3568_UART3_GATE_VAL,
    .pinmux = BOARD_UART3_PINMUX,
    .softrst = { .reg = RK3568_UART3_SOFTRST_REG, .mask = RK3568_UART3_SOFTRST_MASK },
    .rxEchoSuppress = BOARD_UART3_RS485_ECHO,
};
#endif

#if BOARD_UART4_EN
STATIC const UartPortProfile g_uart4Profile = {
    .portNr = 4,
    .base = RK3568_UART4_BASE,
    .intId = RK3568_UART4_INT_ID,
    .clkselReg = RK3568_UART4_CLKSEL_REG,
    .clkselVal = RK3568_UART_CLKSEL_VAL_XIN24M,
    .gateReg = RK3568_UART4_GATE_REG,
    .gateVal = RK3568_UART4_GATE_VAL,
    .pinmux = BOARD_UART4_PINMUX,
    .rxEchoSuppress = BOARD_UART4_RS485_ECHO,
};
#endif

#if BOARD_UART5_EN
STATIC const UartPortProfile g_uart5Profile = {
    .portNr = 5,
    .base = RK3568_UART5_BASE,
    .intId = RK3568_UART5_INT_ID,
    .clkselReg = RK3568_UART5_CLKSEL_REG,
    .clkselVal = RK3568_UART_CLKSEL_VAL_XIN24M,
    .gateReg = RK3568_UART5_GATE_REG,
    .gateVal = RK3568_UART5_GATE_VAL,
    .pinmux = BOARD_UART5_PINMUX,
    .rxEchoSuppress = BOARD_UART5_RS485_ECHO,
};
#endif

#if BOARD_UART6_EN
STATIC const UartPortProfile g_uart6Profile = {
    .portNr = 6,
    .base = RK3568_UART6_BASE,
    .intId = RK3568_UART6_INT_ID,
    .clkselReg = RK3568_UART6_CLKSEL_REG,
    .clkselVal = RK3568_UART_CLKSEL_VAL_XIN24M,
    .gateReg = RK3568_UART6_GATE_REG,
    .gateVal = RK3568_UART6_GATE_VAL,
    .pinmux = BOARD_UART6_PINMUX,
    .rxEchoSuppress = BOARD_UART6_RS485_ECHO,
};
#endif

#if BOARD_UART7_EN
STATIC const UartPortProfile g_uart7Profile = {
    .portNr = 7,
    .base = RK3568_UART7_BASE,
    .intId = RK3568_UART7_INT_ID,
    .clkselReg = RK3568_UART7_CLKSEL_REG,
    .clkselVal = RK3568_UART_CLKSEL_VAL_XIN24M,
    .gateReg = RK3568_UART7_GATE_REG,
    .gateVal = RK3568_UART7_GATE_VAL,
    .pinmux = BOARD_UART7_PINMUX,
    .rxEchoSuppress = BOARD_UART7_RS485_ECHO,
};
#endif

#if BOARD_UART8_EN
STATIC const UartPortProfile g_uart8Profile = {
    .portNr = 8,
    .base = RK3568_UART8_BASE,
    .intId = RK3568_UART8_INT_ID,
    .clkselReg = RK3568_UART8_CLKSEL_REG,
    .clkselVal = RK3568_UART_CLKSEL_VAL_XIN24M,
    .gateReg = RK3568_UART8_GATE_REG,
    .gateVal = RK3568_UART8_GATE_VAL,
    .pinmux = BOARD_UART8_PINMUX,
    .rxEchoSuppress = BOARD_UART8_RS485_ECHO,
};
#endif

#if BOARD_UART9_EN
STATIC const UartPortProfile g_uart9Profile = {
    .portNr = 9,
    .base = RK3568_UART9_BASE,
    .intId = RK3568_UART9_INT_ID,
    .clkselReg = RK3568_UART9_CLKSEL_REG,
    .clkselVal = RK3568_UART_CLKSEL_VAL_XIN24M,
    .gateReg = RK3568_UART9_GATE_REG,
    .gateVal = RK3568_UART9_GATE_VAL,
    .pinmux = BOARD_UART9_PINMUX,
    .rxEchoSuppress = BOARD_UART9_RS485_ECHO,
};
#endif

#define UART_PORT_MIN       RK3568_UART_PORT_MIN
#define UART_PORT_MAX       RK3568_UART_PORT_MAX

/* ===========================================================================
 * Per-port runtime state (statically preallocated, no dynamic memory).
 * ========================================================================= */
#define UART_RX_RING_SIZE   1024U   /* Modbus RTU max frame 256B + burst margin */
#define UART_TX_RING_SIZE   512U
#define UART_RX_RING_MASK   (UART_RX_RING_SIZE - 1U)
#define UART_TX_RING_MASK   (UART_TX_RING_SIZE - 1U)
#define UART_ISR_MAX_ROUNDS 256U    /* bounded IIR dispatch loop (anti-livelock) */

typedef enum {
    UART_PORT_UNINIT = 0,
    UART_PORT_INITED,
} UartPortState;

/* RX side: ring (ISR producer, reader task consumer), "data available"
 * semaphore and the RX-side statistics. */
typedef struct {
    UINT8 buf[UART_RX_RING_SIZE];
    volatile UINT16 head;
    volatile UINT16 tail;
    UINT32 sem;
    volatile UINT32 dropped;      /* ring full */
    volatile UINT32 overrun;      /* FIFO overrun (LSR OE) */
    volatile UINT32 errBytes;     /* OE/PE/FE/BI bytes dropped */
    volatile UINT32 echoDropped;  /* RS485 echo suppression window */
    volatile UINT32 lastLsrErr;
} UartRxCtx;

/* TX side: ring (writer task producer, ISR consumer), "TX progress or
 * drained" semaphore and the TX-side statistics. */
typedef struct {
    UINT8 buf[UART_TX_RING_SIZE];
    volatile UINT16 head;
    volatile UINT16 tail;
    UINT32 sem;
    volatile UINT32 timeoutFlush; /* abort path: frames cut by timeout */
} UartTxCtx;

typedef struct {
    const UartPortProfile *profile;
    volatile UartPortState state;
    UartRxCtx rx;
    UartTxCtx tx;
    /* shadow of IER (DLAB-safe atomic source switching) */
    volatile UINT16 ierShadow;
    /* cached line config (data_bits fixed at init, Q5) */
    UINT8 dataBits;
    /* statistics (shell-observable, §4.5.5) */
    volatile UINT32 isrCount;
} UartPortCtx;

/* Per-port runtime context, indexed by port-1. Slots of ports disabled
 * in uart_board_cfg.h keep profile == NULL and every API returns
 * UART_ERR_NOT_SUP for them. (Ring buffers are preallocated for all SoC
 * ports; shrinking to the enabled set is a possible later optimization.) */
STATIC UartPortCtx g_uartCtx[UART_PORT_MAX] = {
#if BOARD_UART1_EN
    [1 - 1] = { .profile = &g_uart1Profile },
#endif
#if BOARD_UART2_EN
    [2 - 1] = { .profile = &g_uart2Profile },
#endif
#if BOARD_UART3_EN
    [3 - 1] = { .profile = &g_uart3Profile },
#endif
#if BOARD_UART4_EN
    [4 - 1] = { .profile = &g_uart4Profile },
#endif
#if BOARD_UART5_EN
    [5 - 1] = { .profile = &g_uart5Profile },
#endif
#if BOARD_UART6_EN
    [6 - 1] = { .profile = &g_uart6Profile },
#endif
#if BOARD_UART7_EN
    [7 - 1] = { .profile = &g_uart7Profile },
#endif
#if BOARD_UART8_EN
    [8 - 1] = { .profile = &g_uart8Profile },
#endif
#if BOARD_UART9_EN
    [9 - 1] = { .profile = &g_uart9Profile },
#endif
};

/* ISR priority: 0 (highest) - matches the working SGI 7/9 deliveries
 * on this core (their 0xF0-default was measured blocked by PMR=0xF0) */
#define UART_ISR_PRIO       0U

/* Input clock: xin24m (24 MHz), mux defensively forced by the profile. */
#define UART_INPUT_CLOCK    24000000UL

/* tick helpers: timeout granularity is 1 tick (1000 Hz per Q4) */
#define UART_MS_PER_TICK    (((1000U + LOSCFG_BASE_CORE_TICK_PER_SECOND - 1U) / \
                              LOSCFG_BASE_CORE_TICK_PER_SECOND))

/* ===========================================================================
 * Low-level accessors.
 * ========================================================================= */
#define UART_RD(port, reg)      (*(volatile UINT32 *)(UINTPTR)((port)->profile->base + (UINT32)(reg)))
#define UART_WR(port, reg, val) (*(volatile UINT32 *)(UINTPTR)((port)->profile->base + (UINT32)(reg)) = (val))

STATIC INLINE VOID BoardHwW32(UINT32 addr, UINT32 val)
{
    *(volatile UINT32 *)(UINTPTR)addr = val;
}

STATIC INLINE UINT32 BoardHwR32(UINT32 addr)
{
    return *(volatile UINT32 *)(UINTPTR)addr;
}

/* short settle delay between CRU/GRF writes (CAN-chapter convention) */
STATIC INLINE VOID BoardCruDelay(VOID)
{
    for (volatile UINT32 i = 0; i < 100U; i++) {
        __asm__ volatile("nop");
    }
}

STATIC INLINE UINT32 UartMsToTicks(UINT32 ms)
{
    return (ms + UART_MS_PER_TICK - 1U) / UART_MS_PER_TICK;
}

/* IER write via shadow copy (caller holds IntLock or is the ISR) */
STATIC INLINE VOID UartIerWrite(UartPortCtx *port, UINT16 val)
{
    port->ierShadow = val;
    UART_WR(port, UART_REG_IER, val);
}

STATIC INLINE BOOL UartTxBusy(const UartPortCtx *port)
{
    if (port->tx.head != port->tx.tail) {
        return TRUE;
    }
    return ((UART_RD(port, UART_REG_LSR) & UART_LSR_TEMT) == 0);
}

STATIC UINT32 UartLcrCalc(uint8_t dataBits, uart_parity_t parity, uint8_t stopBits)
{
    UINT32 lcr = (UINT32)(dataBits - 5U);

    if (stopBits == 2U) {
        lcr |= UART_LCR_STB;
    }
    if (parity != UART_PARITY_NONE) {
        lcr |= UART_LCR_PEN;
        if (parity == UART_PARITY_EVEN) {
            lcr |= UART_LCR_EPS;
        }
    }
    return lcr;
}

/* legal combos: E-1 / O-1 / N-2 (Q5); data_bits 5~8; baud 1200~115200 */
STATIC BOOL UattrLineValid(uart_parity_t parity, uint8_t stopBits)
{
    switch (parity) {
        case UART_PARITY_EVEN:
        case UART_PARITY_ODD:
            return (stopBits == 1U);
        case UART_PARITY_NONE:
            return (stopBits == 2U);
        default:
            return FALSE;
    }
}

/* divisor with rounding: (24MHz + baud*8) / (baud*16), error <= 0.16% */
STATIC INLINE UINT32 UartDivisorCalc(UINT32 baudrate)
{
    return (UART_INPUT_CLOCK + baudrate * 8UL) / (baudrate * 16UL);
}

/* ===========================================================================
 * Global init/deinit mutex - the only cross-port lock (GICD registers such
 * as IPRIORITYR are read-modify-write across 4 IRQs, §4.5.1).
 * ========================================================================= */
STATIC UINT32 g_uartInitMux;
STATIC BOOL g_uartInitMuxReady;

STATIC UINT32 UartGlobalLock(VOID)
{
    UINT32 intSave = LOS_IntLock();

    if (!g_uartInitMuxReady) {
        if (LOS_MuxCreate(&g_uartInitMux) != LOS_OK) {
            LOS_IntRestore(intSave);
            PRINT_ERR("uart: init mutex create failed\n");
            return LOS_NOK;
        }
        g_uartInitMuxReady = TRUE;
    }
    LOS_IntRestore(intSave);

    return LOS_MuxPend(g_uartInitMux, LOS_WAIT_FOREVER);
}

STATIC VOID UartGlobalUnlock(VOID)
{
    (VOID)LOS_MuxPost(g_uartInitMux);
}

/* Clear the RX/TX ring indices. Caller holds the interrupt lock (or is
 * the only context that can touch the rings). Shared by init (fresh
 * bring-up) and deinit (leave no stale indices behind). */
STATIC INLINE VOID UartRingsReset(UartPortCtx *ctx)
{
    ctx->rx.head = 0;
    ctx->rx.tail = 0;
    ctx->tx.head = 0;
    ctx->tx.tail = 0;
}

/* Zero the shell-observable statistics. init only - deinit keeps the
 * last-run numbers readable until the port is re-initialized. */
STATIC INLINE VOID UartStatsReset(UartPortCtx *ctx)
{
    ctx->isrCount = 0;
    ctx->rx.dropped = 0;
    ctx->rx.overrun = 0;
    ctx->rx.errBytes = 0;
    ctx->rx.echoDropped = 0;
    ctx->tx.timeoutFlush = 0;
    ctx->rx.lastLsrErr = 0;
}

/* ===========================================================================
 * ISR: bounded IIR dispatch loop (NuttX 16550 pattern).
 * ========================================================================= */
STATIC VOID UartRxIsr(UartPortCtx *port)
{
    BOOL gotData = FALSE;
    /* RS485 auto-direction transceivers loop the frame back while we are
     * transmitting: drop RX in the TX-active window if enabled (§4.6.2) */
    BOOL dropEcho = port->profile->rxEchoSuppress && UartTxBusy(port);

    while ((UART_RD(port, UART_REG_LSR) & UART_LSR_DR) != 0) {
        UINT8 ch = (UINT8)UART_RD(port, UART_REG_RBR);
        if (dropEcho) {
            port->rx.echoDropped++;
            continue;
        }
        UINT16 head = port->rx.head;
        if ((UINT16)(head - port->rx.tail) >= UART_RX_RING_SIZE) {
            port->rx.dropped++;
            continue;
        }
        port->rx.buf[head & UART_RX_RING_MASK] = ch;
        port->rx.head = (UINT16)(head + 1U);
        gotData = TRUE;
    }
    if (gotData) {
        (VOID)LOS_SemPost(port->rx.sem);
    }
}

STATIC VOID UartTxIsr(UartPortCtx *port)
{
    UINT32 pushed = 0;

    while (pushed < UART_TX_FIFO_DEPTH) {
        UINT16 tail = port->tx.tail;

        if (tail == port->tx.head) {
            break;
        }
        if ((UART_RD(port, UART_REG_LSR) & UART_LSR_THRE) == 0) {
            break;
        }
        UART_WR(port, UART_REG_THR, port->tx.buf[tail & UART_TX_RING_MASK]);
        port->tx.tail = (UINT16)(tail + 1U);
        pushed++;
    }
    if (pushed > 0) {
        (VOID)LOS_SemPost(port->tx.sem); /* TX ring space freed */
    }
    if (port->tx.tail == port->tx.head) {
        /* ring drained: stop the TX interrupt (no empty-interrupt storm) */
        UartIerWrite(port, (UINT16)(port->ierShadow & ~UART_IER_ETBEI));
        (VOID)LOS_SemPost(port->tx.sem); /* drained event */
    }
}

STATIC VOID UartIsrEntry(VOID *parm)
{
    UartPortCtx *port = &g_uartCtx[(UINT32)(UINTPTR)parm];
    UINT32 rounds = 0;

    /* count every dispatch entry (including idle ones - diagnostic:
     * distinguishes "hook ran" from "hook NULL" on spurious delivery) */
    port->isrCount++;
    while (rounds++ < UART_ISR_MAX_ROUNDS) {
        UINT32 iir = UART_RD(port, UART_REG_IIR);

        if ((iir & UART_IIR_NOT_PENDING) != 0) {
            break;
        }
        switch (iir & UART_IIR_ID_MASK) {
            case UART_IIR_RLS: {
                UINT32 lsr = UART_RD(port, UART_REG_LSR);

                if ((lsr & UART_LSR_ANY_ERR) != 0) {
                    if ((lsr & UART_LSR_OE) != 0) {
                        port->rx.overrun++;
                    }
                    /* discard the corrupt byte, keep counting; Modbus
                     * relies on CRC + t3.5 resync (V0.7) */
                    (VOID)UART_RD(port, UART_REG_RBR);
                    port->rx.errBytes++;
                    port->rx.lastLsrErr = lsr;
                }
                UartRxIsr(port); /* drain the remaining good bytes */
                break;
            }
            case UART_IIR_RDA:
            case UART_IIR_CTI:  /* RX timeout: low-latency read below trigger */
                UartRxIsr(port);
                break;
            case UART_IIR_THRE:
                UartTxIsr(port);
                break;
            default:
                break;
        }
    }
}

/* ===========================================================================
 * GIC SPI ownership (RK3568 AMP): handled by the kernel gic-v3 layer.
 * ArchHwiCreate applies HwiSpiConfigOwn (IGROUPR Group-1 NS claim with a
 * read-modify-write + IROUTER routing to this core + ICFGR trigger mode),
 * see kernel/liteos_m/drivers/interrupt/gic-v3/interrupt.c. NOTE: the
 * kernel-level LOS_HwiCreate only fills the dispatch table and priority -
 * it does NOT call createIrq, so it leaves the SPI routed to the Linux
 * master's default core (observed: uart_test self-loop died on "Unexpected
 * interrupt (irqnr 154)" on the master). Always use ArchHwiCreate here,
 * like net_dwmac.c does. The driver only keeps the crash-safe pending
 * cleanup below. NOTE for future board ports: never write a single-bit
 * value to GICD_IGROUPR directly here - it is a plain RW register and
 * such a write moves every other INTID in the same bank into Group-0,
 * freezing the Linux master with EL3 FIQs (measured on this SoC,
 * INTID bank 128..159 covers UART1-9 + GMAC).
 * ========================================================================= */

/* ArchHwiCreate/ArchHwiDelete live in kernel/liteos_m/arch/arm64/
 * los_common_interrupt.c; no public header declares them (the mica irq
 * backend and net_dwmac.c call them the same way). */
extern UINT32 ArchHwiCreate(HWI_HANDLE_T hwiNum, HWI_PRIOR_T hwiPrio,
                            HWI_MODE_T hwiMode, HWI_PROC_FUNC hwiHandler,
                            HwiIrqParam *irqParam);
extern UINT32 ArchHwiDelete(HWI_HANDLE_T hwiNum, HwiIrqParam *irqParam);

/* ===========================================================================
 * Board-level bring-up: CRU clock gate + parent mux + GRF pinmux.
 * DEPLOYMENT PRECONDITION: the master-side integrator guarantees the
 * selected port's pins are free on the target board before the port id
 * is handed to uart_init (see uart_board_cfg.h reference notes).
 * ========================================================================= */
STATIC VOID UartBoardSetup(const UartPortProfile *profile)
{
    /* 2026-10-21: CRU SOFTRST disabled. The Linux 8250_dw probe already
     * performs the reset during its platform_driver probe (via the reset
     * controller framework). Re-asserting the reset from the slave UNDOES
     * the initialization that enables the C0 pad → UART3 RX input path,
     * causing external loopback failure (rx=0, isr=224 vs 480 baseline).
     * The slave must NOT reset the UART controller after Linux unbind.
     * The softrst struct remains in the profile for documentation. */

    /* clock gates: SET_TO_DISABLE, the profile value writes 0 = enable */
    BoardHwW32(profile->gateReg, profile->gateVal);
    BoardCruDelay();
    /* parent mux -> xin24m (index 2): defensive, boot chain already set */
    BoardHwW32(profile->clkselReg, profile->clkselVal);
    BoardCruDelay();
    /* GRF pinmux + pull: HIWORD16 writes select the m0/m1 group and bias */
    for (UINT32 i = 0; i < (sizeof(profile->pinmux) / sizeof(profile->pinmux[0])); i++) {
        if (profile->pinmux[i].reg != 0) {
            BoardHwW32(profile->pinmux[i].reg, profile->pinmux[i].val);
            BoardCruDelay();
        }
    }
#ifdef LOSCFG_PLATFORM_RK3568_M
    /* Debug: verify the first IOMUX write took effect (read-back check).
     * Catches GRF access issues and silent write failures. */
    {
        UINT32 rd = BoardHwR32(profile->pinmux[0].reg);
        UINT32 expect = profile->pinmux[0].val & 0xFFFF;
        if ((rd & (profile->pinmux[0].val >> 16)) != expect) {
            PRINT_ERR("uart pinmux verify FAIL: reg=0x%x wrote 0x%x read 0x%x\n",
                      profile->pinmux[0].reg, profile->pinmux[0].val, rd);
        }
    }
#endif
}

/* ===========================================================================
 * TX helpers.
 * ========================================================================= */

/*
 * Accept bytes into the TX pipeline: FIFO fast path (TFL-aware) then the
 * ring; blocks on the TX semaphore while the ring is full if a deadline
 * is given. Returns the accepted byte count (<= count).
 */
STATIC UINT16 UartTxAccept(UartPortCtx *port, const UINT8 *buf, UINT16 count, UINT64 deadline)
{
    UINT16 accepted = 0;

    while (accepted < count) {
        UINT32 intSave = LOS_IntLock();

        /* direct FIFO fill (ISR cannot preempt the critical section) */
        UINT32 tfl = UART_RD(port, UART_REG_TFL);
        if (tfl <= UART_TX_FIFO_DEPTH) {
            UINT32 n = UART_TX_FIFO_DEPTH - tfl;

            if (n > (UINT32)(count - accepted)) {
                n = count - accepted;
            }
            while (n-- > 0) {
                UART_WR(port, UART_REG_THR, buf[accepted++]);
            }
        }
        /* overflow into the TX ring */
        while (accepted < count) {
            UINT16 head = port->tx.head;

            if ((UINT16)(head - port->tx.tail) >= UART_TX_RING_SIZE) {
                break;
            }
            port->tx.buf[head & UART_TX_RING_MASK] = buf[accepted++];
            port->tx.head = (UINT16)(head + 1U);
        }
        /* ring non-empty: let the ISR keep feeding the FIFO */
        if (port->tx.head != port->tx.tail) {
            UartIerWrite(port, (UINT16)(port->ierShadow | UART_IER_ETBEI));
        }
        LOS_IntRestore(intSave);

        if (accepted == count) {
            break;
        }
        if (deadline == 0) {
            break; /* non-blocking: single pass */
        }
        UINT64 now = LOS_TickCountGet();
        if (now >= deadline) {
            break;
        }
        if (LOS_SemPend(port->tx.sem, (UINT32)(deadline - now)) != LOS_OK) {
            break; /* budget exhausted */
        }
    }
    return accepted;
}

/*
 * Wait until the TX ring is empty AND the shifter is idle (LSR TEMT).
 * Returns UART_OK or -1 on deadline expiry (caller performs the flush).
 */
STATIC INT32 UartTxDrainWait(UartPortCtx *port, UINT64 deadline)
{
    for (;;) {
        UINT32 intSave = LOS_IntLock();
        BOOL ringEmpty = (port->tx.head == port->tx.tail);
        BOOL temt = ((UART_RD(port, UART_REG_LSR) & UART_LSR_TEMT) != 0);

        LOS_IntRestore(intSave);
        if (ringEmpty && temt) {
            return UART_OK;
        }
        UINT64 now = LOS_TickCountGet();
        if (now >= deadline) {
            return -1;
        }
        if (ringEmpty) {
            /* only the last shifter byte is pending: no IRQ will come,
             * yield-poll at 1 tick granularity (§4.4) */
            (VOID)LOS_TaskDelay(1);
        } else {
            (VOID)LOS_SemPend(port->tx.sem, (UINT32)(deadline - now));
        }
    }
}

/*
 * Abort TX on timeout: reset the TX FIFO, drop the ring, stop ETBEI -
 * the frame stays on the bus as a residual frame (contract semantics)
 * and cannot collide with a master retry as a late complete frame.
 * Returns the byte count that had NOT reached the wire.
 */
STATIC UINT16 UartTxAbrt(UartPortCtx *port)
{
    UINT32 intSave = LOS_IntLock();
    UINT16 ringLeft = (UINT16)(port->tx.head - port->tx.tail);
    UINT32 tfl = UART_RD(port, UART_REG_TFL);
    BOOL temt = ((UART_RD(port, UART_REG_LSR) & UART_LSR_TEMT) != 0);

    port->tx.tail = port->tx.head;
    UartIerWrite(port, (UINT16)(port->ierShadow & ~UART_IER_ETBEI));
    UART_WR(port, UART_REG_FCR, UART_FCR_FIFO_EN | UART_FCR_TX_RST);
    LOS_IntRestore(intSave);

    if (tfl > UART_TX_FIFO_DEPTH) {
        tfl = UART_TX_FIFO_DEPTH; /* defensive clamp on bogus TFL */
    }
    port->tx.timeoutFlush++;
    /* ring remainder + FIFO level + byte still in the shift register */
    return (UINT16)(ringLeft + tfl + (temt ? 0U : 1U));
}

/* ===========================================================================
 * RX helpers.
 * ========================================================================= */
STATIC UINT16 UartRxTake(UartPortCtx *port, UINT8 *buf, UINT16 want)
{
    UINT16 tail = port->rx.tail;
    UINT16 avail = (UINT16)(port->rx.head - tail);

    if (avail > want) {
        avail = want;
    }
    for (UINT16 i = 0; i < avail; i++) {
        buf[i] = port->rx.buf[(UINT16)(tail + i) & UART_RX_RING_MASK];
    }
    port->rx.tail = (UINT16)(tail + avail);
    return avail;
}

/* ===========================================================================
 * Public API (uart_drv.h contract).
 * ========================================================================= */
int32_t uart_init(uint8_t port, const uart_attr_t *attr)
{
    UartPortCtx *ctx = NULL;
    UINT32 intSave;
    UINT32 divisor;
    UINT32 lcr;

    if ((port < UART_PORT_MIN) || ((UINT32)port > UART_PORT_MAX)) {
        return UART_ERR_NOT_SUP; /* UART0: PMUCRU domain, unsupported (§4.2) */
    }
    if (attr == NULL) {
        return UART_ERR_PARAM;
    }
    if ((attr->baudrate < 1200U) || (attr->baudrate > 115200U) ||
        (attr->data_bits < 5U) || (attr->data_bits > 8U) ||
        !UattrLineValid(attr->parity, attr->stop_bits)) {
        return UART_ERR_PARAM;
    }

    ctx = &g_uartCtx[port - 1U];
    if (ctx->profile == NULL) {
        return UART_ERR_NOT_SUP; /* port disabled in uart_board_cfg.h */
    }
    if (ctx->state != UART_PORT_UNINIT) {
        return UART_ERR_STATE; /* duplicate init */
    }
    if (UartGlobalLock() != LOS_OK) {
        return UART_ERR_STATE;
    }

    /* semaphores first: no hardware side effects on the failure path */
    if ((LOS_BinarySemCreate(0, &ctx->rx.sem) != LOS_OK) ||
        (LOS_BinarySemCreate(0, &ctx->tx.sem) != LOS_OK)) {
        PRINT_ERR("uart%u: semaphore exhausted\n", (UINT32)port);
        if (ctx->rx.sem != 0) { /* one of them may exist already */
            (VOID)LOS_SemDelete(ctx->rx.sem);
            ctx->rx.sem = 0;
        }
        UartGlobalUnlock();
        return UART_ERR_STATE;
    }

    /* board bring-up: soft reset + clock gate + xin24m mux + pinmux */
    UartBoardSetup(ctx->profile);

    /* DW 8250 internal soft reset via SRR (offset 0x88, Rockchip HAL
     * convention — HAL_UART_Reset): resets the UART state machine, RX
     * FIFO, and TX FIFO.
     *
     * 2026-10-21: DISABLED. On the "Linux probe + unbind" deployment
     * (uart3=okay in dts, mcs_uart.ko auto-unbind), writing SRR=0x7
     * after the 8250_dw driver's probe RESETS the RX input path that
     * Linux initialized — external loopback fails with rx=0 (isr=224).
     * Without this write, the Linux-initialized UART state is preserved
     * and external loopback passes (isr=480 baseline).
     * The SRR path was an attempt to make the slave self-sufficient
     * (uart3=disabled in dts) but that scenario needs more than SRR
     * (see mcs_uart.ko for the working solution).
     */

    /* 16550 registers; IER stays masked until everything is set up */
    divisor = UartDivisorCalc(attr->baudrate);
    lcr = UartLcrCalc(attr->data_bits, attr->parity, attr->stop_bits);
    intSave = LOS_IntLock();
    ctx->ierShadow = 0;
    UART_WR(ctx, UART_REG_LCR, lcr);          /* DLAB=0 before IER access */
    UART_WR(ctx, UART_REG_IER, 0);
    UART_WR(ctx, UART_REG_FCR, UART_FCR_FIFO_EN | UART_FCR_RX_RST | UART_FCR_TX_RST |
                               UART_FCR_RX_TRIG_1_8);
    UART_WR(ctx, UART_REG_LCR, lcr | UART_LCR_DLAB);
    (VOID)UART_RD(ctx, UART_REG_LCR);                    /* DLAB settle barrier */
    UART_WR(ctx, UART_REG_RBR, divisor & 0xFFU);        /* DLL */
    (VOID)UART_RD(ctx, UART_REG_LCR);                    /* DLL settle barrier */
    UART_WR(ctx, UART_REG_IER, (divisor >> 8) & 0xFFU); /* DLM */
    (VOID)UART_RD(ctx, UART_REG_LCR);                    /* DLM settle barrier */
    UART_WR(ctx, UART_REG_LCR, lcr);
    (VOID)UART_RD(ctx, UART_REG_LCR);                    /* DLAB=0 settle barrier */
    /* drain stale RX and clear pending interrupt sources */
    while ((UART_RD(ctx, UART_REG_LSR) & UART_LSR_DR) != 0) {
        (VOID)UART_RD(ctx, UART_REG_RBR);
    }
    (VOID)UART_RD(ctx, UART_REG_IIR);
    /* rings and counters reset */
    UartRingsReset(ctx);
    ctx->dataBits = attr->data_bits;
    UartStatsReset(ctx);
    LOS_IntRestore(intSave);

    /* GIC: register the SPI - ArchHwiCreate's createIrq (HwiSpiConfigOwn)
     * claims the Group-1 NS ownership (IGROUPR, RMW) and routes it to
     * this core (IROUTER) before the IER sources are enabled */
    HwiIrqParam irqParam = {
        .swIrq = (INT32)ctx->profile->intId,
        .pDevId = (VOID *)(UINTPTR)(port - 1U),
        .pName = "uart",
    };
    if (ArchHwiCreate(ctx->profile->intId, UART_ISR_PRIO, HWI_MODE_TRIGGER_LEVEL,
                      (HWI_PROC_FUNC)UartIsrEntry, &irqParam) != LOS_OK) {
        PRINT_ERR("uart%u: hwi %u create failed\n", (UINT32)port, (UINT32)ctx->profile->intId);
        UartGlobalUnlock();
        return UART_ERR_STATE;
    }
    /* crash-safe pending cleanup (driver-owned, not covered by the kernel
     * claim): a previous run that died mid-transfer can leave the SPI
     * enabled with a latched pending - an enabled level SPI across a
     * slave reboot storms the core with unhandled interrupts before this
     * driver registers its hook. Disable, drop stale pending, then
     * enable clean. */
    (VOID)LOS_HwiDisable(ctx->profile->intId);
    /* LOS_HwiClear uses GICD_ICPENDR which cannot drop a software-sticky
     * pending on this GIC-600 (measured) - GICD_CLRSPI_NSR can and is
     * the per-INTID, side-effect-free counterpart of SETSPI_NSR */
    *(volatile UINT32 *)(UINTPTR)0xFD400048UL = ctx->profile->intId;
    if (LOS_HwiEnable(ctx->profile->intId) != LOS_OK) {
        PRINT_ERR("uart%u: hwi %u enable failed\n", (UINT32)port, (UINT32)ctx->profile->intId);
        (VOID)ArchHwiDelete(ctx->profile->intId, NULL);
        UartGlobalUnlock();
        return UART_ERR_STATE;
    }

    /* enable RX + line status interrupts (last step) */
    intSave = LOS_IntLock();
    UartIerWrite(ctx, UART_IER_ERBFI | UART_IER_ELSI);
    LOS_IntRestore(intSave);

    ctx->state = UART_PORT_INITED;
    UartGlobalUnlock();
    return UART_OK;
}

int32_t uart_deinit(uint8_t port)
{
    UartPortCtx *ctx = NULL;
    UINT32 intSave;

    if ((port < UART_PORT_MIN) || ((UINT32)port > UART_PORT_MAX)) {
        return UART_ERR_NOT_SUP;
    }
    ctx = &g_uartCtx[port - 1U];
    if (ctx->profile == NULL) {
        return UART_ERR_NOT_SUP; /* port disabled in uart_board_cfg.h */
    }
    if (ctx->state == UART_PORT_UNINIT) {
        return UART_OK; /* idempotent: already deinited (§5 re-init cycle) */
    }
    if (ctx->state != UART_PORT_INITED) {
        return UART_ERR_STATE;
    }
    if (UartGlobalLock() != LOS_OK) {
        return UART_ERR_STATE;
    }

    /* stop interrupt sources, then release the GIC SPI (masks it too) */
    intSave = LOS_IntLock();
    UartIerWrite(ctx, 0);
    LOS_IntRestore(intSave);
    (VOID)ArchHwiDelete(ctx->profile->intId, NULL);
    /* leave the GICD line fully clean for the next boot: disable + drop
     * any pending (software-sticky ones need GICD_CLRSPI_NSR, measured) */
    (VOID)LOS_HwiDisable(ctx->profile->intId);
    *(volatile UINT32 *)(UINTPTR)0xFD400048UL = ctx->profile->intId;

    intSave = LOS_IntLock();
    UartRingsReset(ctx);
    LOS_IntRestore(intSave);

    (VOID)LOS_SemDelete(ctx->rx.sem);
    (VOID)LOS_SemDelete(ctx->tx.sem);
    ctx->rx.sem = 0;
    ctx->tx.sem = 0;
    /* clock and pinmux stay configured: re-init is idempotent (§5) */
    ctx->state = UART_PORT_UNINIT;
    UartGlobalUnlock();
    return UART_OK;
}

int32_t uart_read(uint8_t port, uint8_t *buf, uint16_t count, int32_t timeout_ms)
{
    UartPortCtx *ctx = NULL;
    UINT32 total = 0;

    if ((port < UART_PORT_MIN) || ((UINT32)port > UART_PORT_MAX)) {
        return UART_ERR_NOT_SUP;
    }
    ctx = &g_uartCtx[port - 1U];
    if (ctx->profile == NULL) {
        return UART_ERR_NOT_SUP; /* port disabled in uart_board_cfg.h */
    }
    if (ctx->state != UART_PORT_INITED) {
        return UART_ERR_STATE;
    }
    if (count == 0U) {
        return 0;
    }
    if (buf == NULL) {
        return UART_ERR_PARAM;
    }
    if (timeout_ms < 0) {
        return UART_ERR_NOT_SUP; /* infinite wait: not in phase 1 (§5) */
    }

    if (timeout_ms == 0) {
        UINT32 intSave = LOS_IntLock();
        total = UartRxTake(ctx, buf, count);
        LOS_IntRestore(intSave);
        return (int32_t)total;
    }

    UINT64 deadline = LOS_TickCountGet() + UartMsToTicks((UINT32)timeout_ms);
    while (total < count) {
        UINT32 intSave = LOS_IntLock();
        total += UartRxTake(ctx, buf + total, (UINT16)(count - total));
        LOS_IntRestore(intSave);
        if (total == count) {
            return (int32_t)total;
        }
        UINT64 now = LOS_TickCountGet();
        if (now >= deadline) {
            return (int32_t)total;
        }
        if (LOS_SemPend(ctx->rx.sem, (UINT32)(deadline - now)) != LOS_OK) {
            return (int32_t)total; /* timed out */
        }
    }
    return (int32_t)total;
}

int32_t uart_write(uint8_t port, const uint8_t *buf, uint16_t count, int32_t timeout_ms)
{
    UartPortCtx *ctx = NULL;
    UINT16 accepted;
    UINT16 unsent;

    if ((port < UART_PORT_MIN) || ((UINT32)port > UART_PORT_MAX)) {
        return UART_ERR_NOT_SUP;
    }
    ctx = &g_uartCtx[port - 1U];
    if (ctx->profile == NULL) {
        return UART_ERR_NOT_SUP; /* port disabled in uart_board_cfg.h */
    }
    if (ctx->state != UART_PORT_INITED) {
        return UART_ERR_STATE;
    }
    if (count == 0U) {
        return 0;
    }
    if (buf == NULL) {
        return UART_ERR_PARAM;
    }
    if (timeout_ms < 0) {
        return UART_ERR_NOT_SUP;
    }

    if (timeout_ms == 0) {
        /* accept-only, no drain: caller is responsible for tx_flush */
        return (int32_t)UartTxAccept(ctx, buf, count, 0);
    }

    /* one budget for accept + drain (V0.17 contract 6.1.3#3) */
    UINT64 deadline = LOS_TickCountGet() + UartMsToTicks((UINT32)timeout_ms);
    accepted = UartTxAccept(ctx, buf, count, deadline);
    if (accepted < count) {
        /* acceptance timed out: flush the residual frame */
        unsent = UartTxAbrt(ctx);
        return (int32_t)((accepted > unsent) ? (UINT16)(accepted - unsent) : 0U);
    }
    if (UartTxDrainWait(ctx, deadline) != UART_OK) {
        /* drain timed out: count what fully reached the wire, then flush */
        unsent = UartTxAbrt(ctx);
        return (int32_t)((count > unsent) ? (UINT16)(count - unsent) : 0U);
    }
    return (int32_t)count;
}

int32_t uart_tx_flush(uint8_t port)
{
    UartPortCtx *ctx = NULL;

    if ((port < UART_PORT_MIN) || ((UINT32)port > UART_PORT_MAX)) {
        return UART_ERR_NOT_SUP;
    }
    ctx = &g_uartCtx[port - 1U];
    if (ctx->profile == NULL) {
        return UART_ERR_NOT_SUP; /* port disabled in uart_board_cfg.h */
    }
    if (ctx->state != UART_PORT_INITED) {
        return UART_ERR_STATE;
    }

    /* blocks until the last bit has left the shifter; no timeout by
     * contract (Q7: no timeout error code exists, BUSY is not reusable) */
    for (;;) {
        UINT32 intSave = LOS_IntLock();
        BOOL ringEmpty = (ctx->tx.head == ctx->tx.tail);
        BOOL temt = ((UART_RD(ctx, UART_REG_LSR) & UART_LSR_TEMT) != 0);

        LOS_IntRestore(intSave);
        if (ringEmpty && temt) {
            return UART_OK;
        }
        if (ringEmpty) {
            (VOID)LOS_TaskDelay(1);
        } else {
            (VOID)LOS_SemPend(ctx->tx.sem, LOS_WAIT_FOREVER);
        }
    }
}

int32_t uart_set_attr(uint8_t port, uint32_t baudrate, uart_parity_t parity, uint8_t stop_bits)
{
    UartPortCtx *ctx = NULL;
    UINT32 intSave;
    UINT32 divisor;
    UINT32 lcr;

    if ((port < UART_PORT_MIN) || ((UINT32)port > UART_PORT_MAX)) {
        return UART_ERR_NOT_SUP;
    }
    ctx = &g_uartCtx[port - 1U];
    if (ctx->profile == NULL) {
        return UART_ERR_NOT_SUP; /* port disabled in uart_board_cfg.h */
    }
    if (ctx->state != UART_PORT_INITED) {
        return UART_ERR_STATE;
    }
    if ((baudrate < 1200U) || (baudrate > 115200U) || !UattrLineValid(parity, stop_bits)) {
        return UART_ERR_PARAM;
    }

    intSave = LOS_IntLock();
    if (UartTxBusy(ctx)) {
        LOS_IntRestore(intSave);
        return UART_ERR_BUSY; /* transmission in flight */
    }

    divisor = UartDivisorCalc(baudrate);
    lcr = UartLcrCalc(ctx->dataBits, parity, stop_bits);
    /* pause RX/TX interrupt sources around the reconfiguration */
    UartIerWrite(ctx, (UINT16)(ctx->ierShadow & ~(UART_IER_ERBFI | UART_IER_ETBEI | UART_IER_ELSI)));
    UART_WR(ctx, UART_REG_LCR, lcr | UART_LCR_DLAB);
    (VOID)UART_RD(ctx, UART_REG_LCR);                    /* DLAB settle barrier */
    UART_WR(ctx, UART_REG_RBR, divisor & 0xFFU);        /* DLL */
    (VOID)UART_RD(ctx, UART_REG_LCR);                    /* DLL settle barrier */
    UART_WR(ctx, UART_REG_IER, (divisor >> 8) & 0xFFU); /* DLM */
    (VOID)UART_RD(ctx, UART_REG_LCR);                    /* DLM settle barrier */
    UART_WR(ctx, UART_REG_LCR, lcr);
    (VOID)UART_RD(ctx, UART_REG_LCR);                    /* DLAB=0 settle barrier */
    /* drop stale line data around the parameter change (§5) */
    ctx->rx.head = 0;
    ctx->rx.tail = 0;
    UART_WR(ctx, UART_REG_FCR, UART_FCR_FIFO_EN | UART_FCR_RX_RST | UART_FCR_RX_TRIG_1_8);
    while ((UART_RD(ctx, UART_REG_LSR) & UART_LSR_DR) != 0) {
        (VOID)UART_RD(ctx, UART_REG_RBR);
    }
    (VOID)UART_RD(ctx, UART_REG_IIR);
    /* resume RX */
    UartIerWrite(ctx, (UINT16)(ctx->ierShadow | UART_IER_ERBFI | UART_IER_ELSI));
    LOS_IntRestore(intSave);
    return UART_OK; /* register-level only: well under the 1 ms budget */
}

int32_t uart_rx_flush(uint8_t port)
{
    UartPortCtx *ctx = NULL;
    UINT32 intSave;

    if ((port < UART_PORT_MIN) || ((UINT32)port > UART_PORT_MAX)) {
        return UART_ERR_NOT_SUP;
    }
    ctx = &g_uartCtx[port - 1U];
    if (ctx->profile == NULL) {
        return UART_ERR_NOT_SUP; /* port disabled in uart_board_cfg.h */
    }
    if (ctx->state != UART_PORT_INITED) {
        return UART_ERR_STATE;
    }

    intSave = LOS_IntLock();
    ctx->rx.head = 0;
    ctx->rx.tail = 0;
    UART_WR(ctx, UART_REG_FCR, UART_FCR_FIFO_EN | UART_FCR_RX_RST | UART_FCR_RX_TRIG_1_8);
    while ((UART_RD(ctx, UART_REG_LSR) & UART_LSR_DR) != 0) {
        (VOID)UART_RD(ctx, UART_REG_RBR);
    }
    (VOID)UART_RD(ctx, UART_REG_IIR);
    LOS_IntRestore(intSave);
    return UART_OK;
}

/* ===========================================================================
 * Shell self-test carrier (§8.3): registered from main.c after the shell
 * is up (this port registers commands via LOS_ShellCmdReg, the linker
 * table mechanism is not used by liteos_m).
 *
 *   uart_test <port> [baud] [len]   loopback roundtrip (TX-RX jumpered)
 *   uart_test <port> err            API error-path checks (no hardware)
 *   uart_test <port> stat           statistics dump
 * ========================================================================= */
#ifdef LOSCFG_SHELL
#include "los_shell.h"
#include "show.h"
#include "shcmdparse.h"
#include "shell.h"
#include <stdlib.h>
#include <string.h>

#define UART_TEST_LEN_DEFAULT   256U
#define UART_TEST_LEN_MAX       512U

/* line time of a full frame (11 bit/char) + short scheduling margin
 * (board note: the master tick kick rate may lag the slave's 1000 Hz
 * config, real timeouts stretch proportionally - keep the margin tight
 * so verdicts land inside the 10 s rpmsg session window) */
STATIC UINT32 UartTestTimeoutCalc(UINT32 baud, UINT32 len)
{
    UINT32 ms = len * 11000U / baud + 200U;

    if (ms > 15000U) {
        ms = 15000U;
    }
    return ms;
}

/* tick-rate probe measured against the hardware counter (24 MHz wall
 * clock): samples the tick counter across a 2 s busy wait - independent
 * of the tick itself, so it also detects a dead tick (kick absent) */
STATIC UINT32 UartTestTickProbe(VOID)
{
    UINT64 t0, t1, c0, c1;

    __asm__ volatile("isb" ::: "memory");
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(c0));
    t0 = LOS_TickCountGet();
    do {
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(c1));
    } while ((c1 - c0) < (2ULL * 24000000ULL)); /* 2 s wall clock */
    t1 = LOS_TickCountGet();
    PRINTK("tick probe: %llu ticks / 2s wall -> %llu Hz (cfg %u Hz, kick %s)\n",
           (unsigned long long)(t1 - t0),
           (unsigned long long)((t1 - t0) / 2U),
           (UINT32)LOSCFG_BASE_CORE_TICK_PER_SECOND,
           (t1 > t0) ? "RUNNING" : "DEAD");
    return LOS_OK;
}

STATIC VOID UartTestPrintStat(const UartPortCtx *ctx, UINT32 port)
{
    PRINTK("uart%u: state=%u isr=%u rx_drop=%u rx_oe=%u rx_err=%u echo_drop=%u "
           "tx_flush_abrt=%u last_lsr=0x%x\n",
           port, (UINT32)ctx->state, ctx->isrCount, ctx->rx.dropped, ctx->rx.overrun,
           ctx->rx.errBytes, ctx->rx.echoDropped, ctx->tx.timeoutFlush, ctx->rx.lastLsrErr);
}

/* MCR local loopback (test-only, Q11 says the driver never touches MCR -
 * this is bench instrumentation): the 16550 loops TX to RX inside the
 * chip, so the full interrupt data path is verifiable with no wiring. */
#define UART_MCR_LOOP       0x10U

STATIC VOID UartTestMcrLoopSet(UartPortCtx *ctx, BOOL on)
{
    UINT32 intSave = LOS_IntLock();
    UINT32 mcr = UART_RD(ctx, UART_REG_MCR);

    if (on) {
        mcr |= UART_MCR_LOOP;
    } else {
        mcr &= ~UART_MCR_LOOP;
    }
    UART_WR(ctx, UART_REG_MCR, mcr);
    LOS_IntRestore(intSave);
}

STATIC UINT32 UartTestLoopback(UINT32 port, UINT32 baud, UINT32 len, BOOL useMcrLoop)
{
    static UINT8 txbuf[UART_TEST_LEN_MAX];
    static UINT8 rxbuf[UART_TEST_LEN_MAX];
    uart_attr_t attr = {
        .baudrate = baud,
        .data_bits = 8,
        .parity = UART_PARITY_EVEN,
        .stop_bits = 1,
    };
    UINT32 i;
    UINT32 got;
    UINT32 mism = 0;
    int32_t ret;
    UartPortCtx *ctx = &g_uartCtx[port - 1U];

    ret = uart_init((uint8_t)port, &attr);
    if (ret != UART_OK) {
        PRINTK("uart%u init failed: %d\n", port, ret);
        return (UINT32)OS_ERROR;
    }
    if (useMcrLoop) {
        UartTestMcrLoopSet(ctx, TRUE);
    }
    /* data pattern must fit the word length for <8-bit frames */
    UINT8 dataMask = (UINT8)((attr.data_bits >= 8U) ? 0xFFU : ((1U << attr.data_bits) - 1U));
    UINT32 tmoMs = UartTestTimeoutCalc(baud, len);
    PRINTK("uart%u: inited 8-E-1 @%u, looping %u bytes (%s)\n", port, baud, len,
           useMcrLoop ? "MCR internal loop" : "jumper TX-RX");
    for (i = 0; i < len; i++) {
        txbuf[i] = (UINT8)((i + 1U) & dataMask);
    }
    ret = uart_write((uint8_t)port, txbuf, (uint16_t)len, (int32_t)tmoMs);
    if (ret != (int32_t)len) {
        PRINTK("uart%u write ret=%d (expected %u)\n", port, ret, len);
        if (useMcrLoop) {
            UartTestMcrLoopSet(ctx, FALSE);
        }
        (VOID)uart_deinit((uint8_t)port);
        return (UINT32)OS_ERROR;
    }
    PRINTK("uart%u tx done: %u bytes drained (TEMT), start rx\n", port, len);
    got = 0;
    while (got < len) {
        ret = uart_read((uint8_t)port, rxbuf + got, (uint16_t)(len - got), (int32_t)tmoMs);
        if (ret <= 0) {
            break;
        }
        got += (UINT32)ret;
    }
    for (i = 0; i < got; i++) {
        if (rxbuf[i] != txbuf[i]) {
            mism++;
        }
    }
    if ((got == len) && (mism == 0)) {
        PRINTK("uart%u loopback PASS (%u bytes, %u bps)\n", port, len, baud);
    } else {
        PRINTK("uart%u loopback FAIL: got %u/%u, mismatch %u\n", port, got, len, mism);
    }
    UartTestPrintStat(&g_uartCtx[port - 1U], port);
    if (useMcrLoop) {
        UartTestMcrLoopSet(ctx, FALSE);
    }
    (VOID)uart_deinit((uint8_t)port);
    return ((got == len) && (mism == 0)) ? LOS_OK : (UINT32)OS_ERROR;
}

STATIC UINT32 UartTestErrPaths(UINT32 port)
{
    uart_attr_t okAttr = { .baudrate = 9600, .data_bits = 8,
                           .parity = UART_PARITY_EVEN, .stop_bits = 1 };
    uart_attr_t badAttr = okAttr;
    static const uint8_t dummy[4] = {0};
    UINT32 pass = 0;
    UINT32 total = 0;
    int32_t ret;

#define UART_CHECK(desc, expected)            \
    do {                                      \
        total++;                              \
        if ((ret) == (expected)) {            \
            pass++;                           \
        } else {                              \
            PRINTK("  FAIL %-32s ret=%d want=%d\n", desc, ret, (int32_t)(expected)); \
        }                                     \
    } while (0)

    PRINTK("uart%u: API error-path checks\n", port);
    ret = uart_init(0, &okAttr);                       /* UART0 unsupported */
    UART_CHECK("init port0", UART_ERR_NOT_SUP);
    ret = uart_init(10, &okAttr);                      /* out of range */
    UART_CHECK("init port10", UART_ERR_NOT_SUP);
    ret = uart_init((uint8_t)port, NULL);              /* NULL attr */
    UART_CHECK("init NULL attr", UART_ERR_PARAM);
    badAttr.stop_bits = 1;                             /* E-1 -> N-1 illegal */
    badAttr.parity = UART_PARITY_NONE;
    ret = uart_init((uint8_t)port, &badAttr);
    UART_CHECK("init N-1", UART_ERR_PARAM);
    badAttr = okAttr;
    badAttr.baudrate = 600;
    ret = uart_init((uint8_t)port, &badAttr);          /* baud below range */
    UART_CHECK("init baud600", UART_ERR_PARAM);
    badAttr = okAttr;
    badAttr.data_bits = 9;
    ret = uart_init((uint8_t)port, &badAttr);          /* data_bits out of 5~8 */
    UART_CHECK("init dbits9", UART_ERR_PARAM);
    ret = uart_read((uint8_t)port, (uint8_t *)dummy, 4, 100);  /* before init */
    UART_CHECK("read before init", UART_ERR_STATE);
    ret = uart_write((uint8_t)port, dummy, 4, 100);
    UART_CHECK("write before init", UART_ERR_STATE);
    ret = uart_tx_flush((uint8_t)port);
    UART_CHECK("tx_flush before init", UART_ERR_STATE);
    ret = uart_rx_flush((uint8_t)port);
    UART_CHECK("rx_flush before init", UART_ERR_STATE);
    ret = uart_set_attr((uint8_t)port, 115200, UART_PARITY_EVEN, 1);
    UART_CHECK("set_attr before init", UART_ERR_STATE);
    ret = uart_init((uint8_t)port, &okAttr);
    UART_CHECK("init ok", UART_OK);
    ret = uart_init((uint8_t)port, &okAttr);           /* duplicate init */
    UART_CHECK("init twice", UART_ERR_STATE);
    ret = uart_set_attr((uint8_t)port, 115200, UART_PARITY_EVEN, 1);
    UART_CHECK("set_attr ok", UART_OK);
    ret = uart_set_attr((uint8_t)port, 115200, UART_PARITY_NONE, 1);
    UART_CHECK("set_attr N-1", UART_ERR_PARAM);
    ret = uart_set_attr((uint8_t)port, 200000, UART_PARITY_EVEN, 1);
    UART_CHECK("set_attr baud200k", UART_ERR_PARAM);
    ret = uart_read((uint8_t)port, (uint8_t *)dummy, 0, 100);  /* count 0 */
    UART_CHECK("read count0", 0);
    ret = uart_read((uint8_t)port, (uint8_t *)dummy, 4, -1);   /* timeout < 0 */
    UART_CHECK("read tmo<0", UART_ERR_NOT_SUP);
    ret = uart_write((uint8_t)port, dummy, 4, -1);
    UART_CHECK("write tmo<0", UART_ERR_NOT_SUP);
    ret = uart_deinit((uint8_t)port);
    UART_CHECK("deinit ok", UART_OK);
    ret = uart_deinit((uint8_t)port);                  /* duplicate deinit */
    UART_CHECK("deinit twice", UART_ERR_STATE);
#undef UART_CHECK

    PRINTK("uart%u: %u/%u checks passed\n", port, pass, total);
    return (pass == total) ? LOS_OK : (UINT32)OS_ERROR;
}

/* hardware-level diagnostic (no tick dependency, busy-wait timed):
 *  1) tick kick health, 2) raw THR shift test (sclk alive?), 3) SPI
 *  software-trigger delivery proof, 4) driver write verdict */
STATIC UINT32 UartTestDiag(UINT32 port)
{
    static UINT8 diagBuf[64];
    uart_attr_t attr = { .baudrate = 115200, .data_bits = 8,
                         .parity = UART_PARITY_EVEN, .stop_bits = 1 };
    UartPortCtx *ctx = &g_uartCtx[port - 1U];
    UINT64 c0, c1;
    UINT32 resp = 0;
    UINT32 i;
    int32_t ret;

    ret = uart_init((uint8_t)port, &attr);
    if (ret != UART_OK) {
        PRINTK("diag: init ret=%d\n", ret);
        return (UINT32)OS_ERROR;
    }
    /* bench guard: with nothing wired to the RX pin the line floats low
     * into a break condition and ERBFI+ELSI storm the core with RLS
     * interrupts - mask the RX sources immediately (TX path untouched)
     * so the TX-side verification below runs clean. Wired links do not
     * need this (RS-485/TTL idles high). */
    UINT32 intSave = LOS_IntLock();
    UartIerWrite(ctx, (UINT16)(ctx->ierShadow & ~(UART_IER_ERBFI | UART_IER_ELSI)));
    LOS_IntRestore(intSave);
    PRINTK("diag: init ok (rx sources masked), probing tick\n");
    UINT32 base = ctx->profile->base;

    /* 1. tick kick health */
    (VOID)UartTestTickProbe();

    /* No layered-delivery probe here: the software
     * pend of our level SPI via GICD_SETSPI_NSR re-asserts after EOI on
     * this GIC-600 (measured, see the post-mortem comment below) and
     * loops the prio-0 ISR forever, starving the rpmsg SGI kick and
     * getting the slave powered off by the master keep-alive. The
     * routing itself is already exercised by every real (hardware-
     * sourced) interrupt in the other test modes. */

    /* 2. raw shift test: 4 bytes straight into THR, then wall-clock wait */
    __asm__ volatile("isb" ::: "memory");
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(c0));
    for (i = 0; i < 4U; i++) {
        *(volatile UINT32 *)(UINTPTR)(base + UART_REG_THR) = 0x55U + i;
    }
    do {
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(c1));
    } while ((c1 - c0) < (24000000ULL / 4ULL)); /* 250 ms wall */
    UINT32 lsr = *(volatile UINT32 *)(UINTPTR)(base + UART_REG_LSR);
    UINT32 tfl = *(volatile UINT32 *)(UINTPTR)(base + UART_REG_TFL);
    PRINTK("diag: raw-shift LSR=0x%x TFL=%u (bit5 THRE / bit6 TEMT set => sclk ok)\n",
           lsr, tfl);

    /* 3. GIC claim state snapshot (no side effects: the software SPI
     * trigger GICD_SETSPI_NSR is deliberately NOT used - on this GIC-600
     * a software-pended level SPI re-asserts after EOI and loops the
     * core forever, which is exactly what wedged earlier sessions) */
    {
        UINT32 intId = ctx->profile->intId;
        volatile UINT32 *isenabler = (volatile UINT32 *)(UINTPTR)(0xFD400000UL + 0x0100UL + (intId / 32U) * 4U);
        volatile UINT32 *ipriorityr = (volatile UINT32 *)(UINTPTR)(0xFD400000UL + 0x0400UL + (intId / 4U) * 4U);
        volatile UINT32 *ispendr    = (volatile UINT32 *)(UINTPTR)(0xFD400000UL + 0x0200UL + (intId / 32U) * 4U);
        volatile UINT32 *igroupr    = (volatile UINT32 *)(UINTPTR)(0xFD400000UL + 0x0080UL + (intId / 32U) * 4U);
        volatile UINT64 *irouter    = (volatile UINT64 *)(UINTPTR)(0xFD400000UL + 0x6000UL + (UINT32)intId * 8U);
        UINT64 pmr;
        __asm__ volatile("mrs %0, S3_0_C4_C6_0" : "=r"(pmr)); /* ICC_PMR_EL1 */
        PRINTK("diag: gic spi%u en=%u prio=0x%x pend=%u grp=0x%x route=0x%llx pmr=0x%llx\n",
               intId, ((*isenabler >> (intId % 32U)) & 1U),
               ((*ipriorityr >> ((intId % 4U) * 8U)) & 0xFFU),
               ((*ispendr >> (intId % 32U)) & 1U),
               *igroupr,
               (unsigned long long)*irouter, (unsigned long long)pmr);
        PRINTK("diag: attempting write (wedges here => delivery still broken)\n");
    }

    /* 4. driver-level write (needs tick + SPI + sclk to finish) */
    for (i = 0; i < sizeof(diagBuf); i++) {
        diagBuf[i] = (UINT8)(i + 1U);
    }
    ret = uart_write((uint8_t)port, diagBuf, sizeof(diagBuf), 2000);
    PRINTK("diag: uart_write(64) ret=%d (expect 64)\n", ret);

    /* 5. post-mortem: did the THRE ever pend at the GICD? */
    {
        UINT32 intId = ctx->profile->intId;
        volatile UINT32 *ispendr = (volatile UINT32 *)(UINTPTR)(0xFD400000UL + 0x0200UL + (intId / 32U) * 4U);
        PRINTK("diag: post gic pend=%u IIR=0x%x IER=0x%x LSR=0x%x TFL=%u\n",
               ((*ispendr >> (intId % 32U)) & 1U),
               UART_RD(ctx, UART_REG_IIR), UART_RD(ctx, UART_REG_IER),
               UART_RD(ctx, UART_REG_LSR), UART_RD(ctx, UART_REG_TFL));
    }

    UartTestPrintStat(ctx, port);
    (VOID)uart_deinit((uint8_t)port);
    return LOS_OK;
}

/*
 * Line-config matrix on the MCR internal loop (no wiring): every legal
 * combo E-1/O-1/N-2 x data_bits 5..8 must round-trip error free, and an
 * online set_attr() baud switch must keep the link clean (T1/T2 subset).
 */
STATIC UINT32 UartTestMatrix(UINT32 port)
{
    static const uart_attr_t combos[] = {
        { 115200, 8, UART_PARITY_EVEN, 1 }, { 115200, 8, UART_PARITY_ODD, 1 },
        { 115200, 8, UART_PARITY_NONE, 2 }, { 115200, 7, UART_PARITY_EVEN, 1 },
        { 115200, 7, UART_PARITY_ODD, 1 },  { 115200, 7, UART_PARITY_NONE, 2 },
        { 115200, 6, UART_PARITY_EVEN, 1 }, { 115200, 6, UART_PARITY_ODD, 1 },
        { 115200, 6, UART_PARITY_NONE, 2 }, { 115200, 5, UART_PARITY_EVEN, 1 },
        { 115200, 5, UART_PARITY_ODD, 1 },  { 115200, 5, UART_PARITY_NONE, 2 },
    };
    static const UINT32 baudSteps[] = { 115200, 9600, 1200, 115200 };
    static UINT8 mtxBuf[64];
    UINT32 pass = 0;
    UINT32 total = 0;
    UINT32 c;
    UINT32 s;
    UINT32 len = 32U;

    for (c = 0; c < (sizeof(combos) / sizeof(combos[0])); c++) {
        const uart_attr_t *attr = &combos[c];
        UartPortCtx *ctx = &g_uartCtx[port - 1U];
        UINT8 dataMask = (UINT8)((attr->data_bits >= 8U) ? 0xFFU : ((1U << attr->data_bits) - 1U));
        BOOL comboOk = TRUE;
        total++;

        int32_t ret = uart_init((uint8_t)port, attr);
        if (ret != UART_OK) {
            PRINTK("  %u-%s-%u: init FAIL %d\n", attr->data_bits,
                   (attr->parity == UART_PARITY_EVEN) ? "E" :
                   ((attr->parity == UART_PARITY_ODD) ? "O" : "N"),
                   attr->stop_bits, ret);
            pass += 0;
            continue;
        }
        UartTestMcrLoopSet(ctx, TRUE);
        for (UINT32 i = 0; i < len; i++) {
            mtxBuf[i] = (UINT8)((i + 0xA5U) & dataMask);
        }
        /* step through the baud rates with online set_attr */
        UINT32 curBaud = attr->baudrate; /* track the LIVE baud: step[3]
         * intentionally returns to attr->baudrate, comparing against it
         * would SKIP that set_attr and leave the previous (slow) baud */
        for (s = 0; s < (sizeof(baudSteps) / sizeof(baudSteps[0])); s++) {
            if (baudSteps[s] != curBaud) {
                ret = uart_set_attr((uint8_t)port, baudSteps[s], attr->parity, attr->stop_bits);
                if (ret != UART_OK) {
                    PRINTK("    [c%u s%u] set_attr(%u) ret=%d\n", c, s, baudSteps[s], ret);
                    comboOk = FALSE;
                    break;
                }
                curBaud = baudSteps[s];
            }
            ret = uart_write((uint8_t)port, mtxBuf, (uint16_t)len,
                             (int32_t)UartTestTimeoutCalc(baudSteps[s], len));
            if (ret != (int32_t)len) {
                PRINTK("    [c%u s%u] write(%u) ret=%d\n", c, s, baudSteps[s], ret);
                comboOk = FALSE;
                break;
            }
            static UINT8 rxBuf[64];
            UINT32 got = 0;
            while (got < len) {
                ret = uart_read((uint8_t)port, rxBuf + got, (uint16_t)(len - got),
                                (int32_t)UartTestTimeoutCalc(baudSteps[s], len));
                if (ret <= 0) {
                    break;
                }
                got += (UINT32)ret;
            }
            if (got != len) {
                PRINTK("    [c%u s%u] got=%u/%u\n", c, s, got, len);
                comboOk = FALSE;
                break;
            }
            for (UINT32 i = 0; i < len; i++) {
                if (rxBuf[i] != mtxBuf[i]) {
                    PRINTK("    [c%u s%u] mism@%u: exp %02x got %02x | head %02x %02x %02x %02x\n",
                           c, s, i, mtxBuf[i], rxBuf[i], rxBuf[0], rxBuf[1], rxBuf[2], rxBuf[3]);
                    comboOk = FALSE;
                    break;
                }
            }
            if (!comboOk) {
                break;
            }
        }
        UartTestMcrLoopSet(ctx, FALSE);
        (VOID)uart_deinit((uint8_t)port);
        if (comboOk) {
            pass++;
            PRINTK("  %u-%s-%u: PASS (115200/9600/1200 switch)\n", attr->data_bits,
                   (attr->parity == UART_PARITY_EVEN) ? "E" :
                   ((attr->parity == UART_PARITY_ODD) ? "O" : "N"),
                   attr->stop_bits);
        } else {
            PRINTK("  %u-%s-%u: FAIL\n", attr->data_bits,
                   (attr->parity == UART_PARITY_EVEN) ? "E" :
                   ((attr->parity == UART_PARITY_ODD) ? "O" : "N"),
                   attr->stop_bits);
        }
    }
    PRINTK("matrix: %u/%u combos passed\n", pass, total);
    return (pass == total) ? LOS_OK : (UINT32)OS_ERROR;
}

/* TX-only write verification (bench isolation): init with the RX
 * interrupt sources masked (dangling RX pins would otherwise dominate
 * with break storms), then a driver-level timed write + stats dump.
 * The frame physically leaves the TX pin - no MCR loop involved. */
STATIC UINT32 UartTestTxOnly(UINT32 port, UINT32 baud, UINT32 len)
{
    static UINT8 txbuf[UART_TEST_LEN_MAX];
    uart_attr_t attr = { .baudrate = baud, .data_bits = 8,
                         .parity = UART_PARITY_EVEN, .stop_bits = 1 };
    UartPortCtx *ctx = &g_uartCtx[port - 1U];
    int32_t ret;
    UINT32 i;
    UINT32 intSave;
    UINT32 tmoMs = UartTestTimeoutCalc(baud, len);

    ret = uart_init((uint8_t)port, &attr);
    if (ret != UART_OK) {
        PRINTK("uart%u init failed: %d\n", port, ret);
        return (UINT32)OS_ERROR;
    }
    intSave = LOS_IntLock();
    UartIerWrite(ctx, (UINT16)(ctx->ierShadow & ~(UART_IER_ERBFI | UART_IER_ELSI)));
    LOS_IntRestore(intSave);
    for (i = 0; i < len; i++) {
        txbuf[i] = (UINT8)(i + 1U);
    }
    PRINTK("uart%u: tx-only %u bytes @%u (rx masked), tmo %u ms\n", port, len, baud, tmoMs);
    ret = uart_write((uint8_t)port, txbuf, (uint16_t)len, (int32_t)tmoMs);
    PRINTK("uart%u: uart_write ret=%d (expect %u)\n", port, ret, len);
    UartTestPrintStat(ctx, port);
    (VOID)uart_deinit((uint8_t)port);
    return (ret == (int32_t)len) ? LOS_OK : (UINT32)OS_ERROR;
}

UINT32 OsShellCmdUartTest(UINT32 argc, const CHAR **argv)
{
    UINT32 port;
    UINT32 baud = 115200;
    UINT32 len = UART_TEST_LEN_DEFAULT;
    CHAR *endPtr = NULL;

    if ((argc == 0U) || (argv[0] == NULL)) {
        PRINTK("usage:\n");
        PRINTK("  uart_test <port> [baud] [len]   loopback test (external TX-RX jumper)\n");
        PRINTK("  uart_test <port> self [baud] [len]  MCR internal loop (no wiring)\n");
         PRINTK("  uart_test <port> matrix         line-combo matrix on internal loop\n");
         PRINTK("  uart_test <port> tx [baud] [len] TX-only write check (rx masked)\n");
         PRINTK("  uart_test <port> err            API error-path checks\n");
        PRINTK("  uart_test <port> stat           statistics\n");
        PRINTK("  uart_test <port> diag           TX-path hardware diagnostic\n");
        PRINTK("  uart_test <port> tick           tick kick probe\n");
        return (UINT32)OS_ERROR;
    }
    port = (UINT32)strtoul(argv[0], &endPtr, 0);
    if ((endPtr == argv[0]) || (port < UART_PORT_MIN) || (port > UART_PORT_MAX)) {
        PRINTK("uart_test: bad port '%s' (1..9)\n", argv[0]);
        return (UINT32)OS_ERROR;
    }
    if (g_uartCtx[port - 1U].profile == NULL) {
        PRINTK("uart_test: uart%u not enabled on this board (uart_board_cfg.h)\n", port);
        return (UINT32)OS_ERROR;
    }
    if ((argc >= 2U) && (argv[1] != NULL) && (strncmp(argv[1], "err", 3) == 0)) {
        return UartTestErrPaths(port);
    }
    if ((argc >= 2U) && (argv[1] != NULL) && (strncmp(argv[1], "stat", 4) == 0)) {
        UartTestPrintStat(&g_uartCtx[port - 1U], port);
        return LOS_OK;
    }
    if ((argc >= 2U) && (argv[1] != NULL) && (strncmp(argv[1], "tick", 4) == 0)) {
        return UartTestTickProbe();
    }
    if ((argc >= 2U) && (argv[1] != NULL) && (strncmp(argv[1], "diag", 4) == 0)) {
        return UartTestDiag(port);
    }
    if ((argc >= 2U) && (argv[1] != NULL) && (strncmp(argv[1], "self", 4) == 0)) {
        /* MCR internal loop: no wiring needed; args shift by one */
        UINT32 selfBaud = (argc >= 3U) ? (UINT32)strtoul(argv[2], &endPtr, 0) : 115200U;
        UINT32 selfLen = (argc >= 4U) ? (UINT32)strtoul(argv[3], &endPtr, 0) : UART_TEST_LEN_DEFAULT;

        if ((selfBaud < 1200U) || (selfBaud > 115200U) || (selfLen == 0U) || (selfLen > UART_TEST_LEN_MAX)) {
            PRINTK("uart_test: bad self args (baud 1200..115200, len 1..%u)\n", UART_TEST_LEN_MAX);
            return (UINT32)OS_ERROR;
        }
        return UartTestLoopback(port, selfBaud, selfLen, TRUE);
    }
    if ((argc >= 2U) && (argv[1] != NULL) && (strncmp(argv[1], "matrix", 6) == 0)) {
        return UartTestMatrix(port);
    }
    if ((argc >= 2U) && (argv[1] != NULL) && (strncmp(argv[1], "tx", 2) == 0)) {
        UINT32 txBaud = (argc >= 3U) ? (UINT32)strtoul(argv[2], &endPtr, 0) : 115200U;
        UINT32 txLen = (argc >= 4U) ? (UINT32)strtoul(argv[3], &endPtr, 0) : UART_TEST_LEN_DEFAULT;

        if ((txBaud < 1200U) || (txBaud > 115200U) || (txLen == 0U) || (txLen > UART_TEST_LEN_MAX)) {
            PRINTK("uart_test: bad tx args (baud 1200..115200, len 1..%u)\n", UART_TEST_LEN_MAX);
            return (UINT32)OS_ERROR;
        }
        return UartTestTxOnly(port, txBaud, txLen);
    }
    if (argc >= 2U) {
        baud = (UINT32)strtoul(argv[1], &endPtr, 0);
        if ((endPtr == argv[1]) || (baud < 1200U) || (baud > 115200U)) {
            PRINTK("uart_test: bad baud '%s'\n", argv[1]);
            return (UINT32)OS_ERROR;
        }
    }
    if (argc >= 3U) {
        len = (UINT32)strtoul(argv[2], &endPtr, 0);
        if ((endPtr == argv[2]) || (len == 0U) || (len > UART_TEST_LEN_MAX)) {
            PRINTK("uart_test: bad len '%s' (1..%u)\n", argv[2], UART_TEST_LEN_MAX);
            return (UINT32)OS_ERROR;
        }
    }
    return UartTestLoopback(port, baud, len, FALSE);
}

UINT32 UartShellCmdRegister(VOID)
{
    return osCmdReg(CMD_TYPE_EX, "uart_test", XARGS, (CmdCallBackFunc)OsShellCmdUartTest);
}
#endif /* LOSCFG_SHELL */

#endif /* LOSCFG_RK3568_UART */
