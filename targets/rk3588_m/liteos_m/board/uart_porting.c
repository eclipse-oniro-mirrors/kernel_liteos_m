/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * RK3568 UART3 console (16550-compatible IP, 32-bit register access).
 *
 * Lessons from the ws63 UART FIFO issue applied here:
 *  - all register accesses are 32-bit wide (Rockchip DW 8250);
 *  - FIFO is explicitly enabled and triggered, TX drained via LSR;
 *  - divisor latch setup via LCR DLAB sequence, 8N1.
 *
 * The UART input clock is the 24MHz xin osc (default parent in CRU).
 * CRU/GRF clocks are left as configured by U-Boot/TF-A (plan 2.1-5).
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

VOID UartInit(VOID)
{
#ifdef LOSCFG_MCS
    /*
     * MCS AMP mode: UART2 is shared with the Linux console (ttyFIQ0 on
     * the master core). The master core already configured the line
     * (1500000 8N1, FIFOs on) and its FIQ debugger depends on the RX
     * interrupt (IER bit0). Do NOT touch IER/DLL/LCR/MCR/FCR and do NOT
     * drain RX here - that would break the master console input path
     * and steal its keystrokes. The slave core only performs polled TX
     * via UartPutc().
     */
#else
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

INT32 UartPutc(INT32 c, VOID *file)
{
    (VOID)file;
    if (c == '\n') {
        while ((*Reg32(UART_LSR) & UART_LS_TX_EMPTY) == 0) { }
        *Reg32(UART_THR) = (UINT32)'\r';
    }
    while ((*Reg32(UART_LSR) & UART_LS_TX_EMPTY) == 0) { }
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
