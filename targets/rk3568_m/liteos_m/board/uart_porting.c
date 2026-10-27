/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * RK3568 UART3 console (16550-compatible IP): 32-bit register access
 * (Rockchip DW 8250), FIFO on, DLAB divisor sequence, 8N1.  Input clock
 * is the 24MHz xin osc; CRU/GRF clocks stay as configured by U-Boot/TF-A.
 */

#include "los_typedef.h"
#include "los_debug.h"
#include "los_config.h"
#ifdef LOSCFG_SHELL
#include "los_event.h"
#include "los_task.h"
#endif
#include "uart.h"
#include "soc.h"
#include <stdarg.h>
#include "securec.h"

/* 16550 registers (byte offsets, 32-bit access). */
#define UART_RBR        (UART_BASE + 0x00) /* RX buffer (DLAB=0) */
#define UART_THR        (UART_BASE + 0x00) /* TX buffer (DLAB=0) */
#define UART_DLL        (UART_BASE + 0x00) /* divisor low (DLAB=1) */
#define UART_IER        (UART_BASE + 0x04) /* int enable (DLAB=0) */
#define UART_DLM        (UART_BASE + 0x04) /* divisor high (DLAB=1) */
#define UART_IIR        (UART_BASE + 0x08) /* int identification */
#define UART_FCR        (UART_BASE + 0x08) /* FIFO control */
#define UART_LCR        (UART_BASE + 0x0C) /* line control */
#define UART_MCR        (UART_BASE + 0x10) /* modem control */
#define UART_LSR        (UART_BASE + 0x14) /* line status */
#define UART_SCR        (UART_BASE + 0x1C) /* scratch */

/* LSR bits */
#define UART_LS_RX_READY    0x01U
#define UART_LS_TX_EMPTY    0x20U
#define UART_LS_TX_IDLE     0x40U

/* LCR values */
#define UART_LCR_DLAB       0x80U
#define UART_LCR_8N1        0x03U

/* FCR: enable FIFO + clear both FIFOs + RX trigger 1/8 (default) */
#define UART_FCR_ENABLE     0x07U
#define UART_FCR_RX_1_8     (0x00U << 6)

/* MCR: DTR + RTS (keep the peer happy when wired) */
#define UART_MCR_DTR_RTS    0x03U

STATIC INLINE volatile UINT32 *Reg32(UINTPTR addr)
{
    return (volatile UINT32 *)addr;
}

/* MCS AMP mode: UART2 is shared with the Linux console (ttyFIQ0 on the
 * master core), which already configured the line (1500000 8N1, FIFOs on)
 * and whose FIQ debugger depends on the RX interrupt.  Do NOT touch
 * IER/DLL/LCR/MCR/FCR and do NOT drain RX here - the slave core only
 * performs polled TX via UartPutc().  Non-MCS builds configure from scratch. */
VOID UartInit(VOID)
{
#ifndef LOSCFG_MCS
    UINT32 divisor = (UINT32)(UART_INPUT_CLOCK / (UART_BAUDRATE * 16UL));

    /* mask all interrupt sources */
    *Reg32(UART_IER) = 0x00;

    /* baud rate: DLAB sequence */
    *Reg32(UART_LCR) = UART_LCR_8N1 | UART_LCR_DLAB;
    *Reg32(UART_DLL) = divisor & 0xFF;
    *Reg32(UART_DLM) = (divisor >> 8) & 0xFF;
    *Reg32(UART_LCR) = UART_LCR_8N1;

    /* modem + FIFO */
    *Reg32(UART_MCR) = UART_MCR_DTR_RTS;
    *Reg32(UART_FCR) = UART_FCR_ENABLE | UART_FCR_RX_1_8;

    /* drain pending RX and clear interrupts */
    (VOID)*Reg32(UART_RBR);
    (VOID)*Reg32(UART_IIR);
#endif
}

/* AMP hardening: the console UART is owned by the Linux master
 * (fiq-debugger); if it leaves the line in a state where LSR.THRE never
 * reads 1, an unbounded polled TX would wedge this core forever.  Bound
 * the wait: after ~1M spins (~0.5s) enter stall mode - write THR blind
 * and keep running; a one-time marker is emitted on recovery. */
#define UART_TX_SPIN_LIMIT 1000000U
STATIC volatile UINT32 g_uartTxStallMode = 0;
STATIC volatile UINT32 g_uartTxStallSeen = 0;

STATIC VOID UartPutcWait(VOID)
{
    UINT32 spins = 0;

    while ((*Reg32(UART_LSR) & UART_LS_TX_EMPTY) == 0) {
        if ((g_uartTxStallMode != 0) || (++spins > UART_TX_SPIN_LIMIT)) {
            g_uartTxStallMode = 1;
            return;
        }
    }
    if (g_uartTxStallMode != 0) {
        g_uartTxStallMode = 0;
        if (g_uartTxStallSeen == 0) {
            g_uartTxStallSeen = 1;
            /* recovery marker: blind writes only, we are mid-recovery */
            *Reg32(UART_THR) = (UINT32)'[';
            *Reg32(UART_THR) = (UINT32)'U';
            *Reg32(UART_THR) = (UINT32)'A';
            *Reg32(UART_THR) = (UINT32)'R';
            *Reg32(UART_THR) = (UINT32)'T';
            *Reg32(UART_THR) = (UINT32)'-';
            *Reg32(UART_THR) = (UINT32)'R';
            *Reg32(UART_THR) = (UINT32)'E';
            *Reg32(UART_THR) = (UINT32)'C';
            *Reg32(UART_THR) = (UINT32)']';
            *Reg32(UART_THR) = (UINT32)'\r';
            *Reg32(UART_THR) = (UINT32)'\n';
        }
    }
}

INT32 UartPutc(INT32 c, VOID *file)
{
    (VOID)file;
    if (c == '\n') {
        UartPutcWait();
        *Reg32(UART_THR) = (UINT32)'\r';
    }
    UartPutcWait();
    *Reg32(UART_THR) = (UINT32)(UINT8)c;
    return c;
}

VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock)
{
    (VOID)isLock;
    for (UINT32 i = 0; i < len; i++) {
        (VOID)UartPutc((INT32)s[i], NULL);
    }
}

INT32 UartGetc(VOID)
{
    if ((*Reg32(UART_LSR) & UART_LS_RX_READY) != 0) {
        return (INT32)(*Reg32(UART_RBR) & 0xFF);
    }
    return -1;
}

VOID HalConsoleOutput(LogModuleType type, INT32 level, const CHAR *fmt, ...)
{
    (VOID)type;
    (VOID)level;
    CHAR buf[256];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf_s(buf, sizeof(buf), sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (len > 0) {
        UartPuts(buf, (UINT32)len, UART_WITHOUT_LOCK);
    }
}

#ifdef LOSCFG_SHELL
/*
 * Shell input poll task: polls UART3 LSR for RX data and signals
 * g_shellInputEvent to wake up ShellTaskEntry (shmsg.c), which then calls
 * UartGetc() to drain the RX FIFO.
 */
extern EVENT_CB_S g_shellInputEvent;

STATIC VOID ShellInputPollTask(VOID)
{
    while (1) {
        if ((*Reg32(UART_LSR) & UART_LS_RX_READY) != 0) {
            (VOID)LOS_EventWrite(&g_shellInputEvent, 0x1);
        }
        LOS_TaskDelay(2); /* 2 ticks */
    }
}

VOID ShellInputPollTaskInit(VOID)
{
    UINT32 taskId;
    TSK_INIT_PARAM_S task = {0};
    task.pfnTaskEntry = (TSK_ENTRY_FUNC)ShellInputPollTask;
    task.uwStackSize = 0x800;
    task.pcName = "ShellInputPoll";
    task.usTaskPrio = 9;
    (VOID)LOS_TaskCreate(&taskId, &task);
}
#endif
