/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: WS63 UART porting (adapted from 2.08.5 uart_porting.c)
 *
 * Provides UartPuts, sram_put_byte/get_byte, vsnprintf_s, memset_s, memcpy_s —
 * all pure hardware operations, no kernel API.
 */

#include "los_compiler.h"
#include "los_debug.h"
#include "los_interrupt.h"
#include "soc.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#ifdef LOSCFG_SHELL
#include "los_task.h"
#include "uart.h"
#endif

#ifdef LOSCFG_KERNEL_RAM_PRESET_TINY
#define BOARD_PRINTF_BUF_SIZE 128
#define BOARD_SPRINTF_MAX     512

static char g_board_printf_buf[BOARD_PRINTF_BUF_SIZE];
#endif

void sram_put_byte(void *addr, uint8_t c)
{
    volatile uint32_t *p = (volatile uint32_t *)((uintptr_t)addr & ~3u);
    unsigned int shift = ((uintptr_t)addr & 3u) * 8;
    uint32_t mask = 0xffu << shift;
    *p = (*p & ~mask) | ((uint32_t)c << shift);
}

uint8_t sram_get_byte(const void *addr)
{
    volatile uint32_t *p = (volatile uint32_t *)((uintptr_t)addr & ~3u);
    unsigned int shift = ((uintptr_t)addr & 3u) * 8;
    return (uint8_t)((*p >> shift) & 0xff);
}

static int put_str(char *buf, unsigned int max, unsigned int *pos, const char *s)
{
    int n = 0;
    while (s && sram_get_byte(&s[n]) && *pos < max) {
        sram_put_byte(&buf[*pos], sram_get_byte(&s[n]));
        (*pos)++;
        n++;
    }
    return n;
}

static int put_uint(char *buf, unsigned int max, unsigned int *pos,
                    unsigned int val, unsigned int base, int upper,
                    int width, int zero_pad)
{
    char tmp[16];
    int len = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (val == 0) {
        sram_put_byte(&tmp[0], '0');
        len = 1;
    } else {
        while (val && len < 16) {
            sram_put_byte(&tmp[len], digits[val % base]);
            val /= base;
            len++;
        }
    }
    while (len < width && *pos < max) {
        sram_put_byte(&buf[*pos], zero_pad ? '0' : ' ');
        (*pos)++;
        width--;
    }
    int n = 0;
    while (len > 0 && *pos < max) {
        len--;
        sram_put_byte(&buf[*pos], sram_get_byte(&tmp[len]));
        (*pos)++;
        n++;
    }
    return n;
}

static int put_int(char *buf, unsigned int max, unsigned int *pos,
                   int val, int width, int zero_pad)
{
    int n = 0;
    if (val < 0) {
        if (*pos < max) {
            sram_put_byte(&buf[*pos], '-');
            (*pos)++;
        }
        n++;
        val = -val;
    }
    n += put_uint(buf, max, pos, (unsigned int)val, 10, 0, width > 0 ? width - n : 0, zero_pad);
    return n;
}

__attribute__((weak)) int vsnprintf_s(char *dest, unsigned int destMax, unsigned int count, const char *format, va_list argptr)
{
    if (!dest || !destMax || !format || !count) return 0;
    unsigned int max = count < destMax ? count : destMax;
    unsigned int pos = 0;
    const char *fmt = format;

    while (sram_get_byte(fmt) && pos < max) {
        char c = sram_get_byte(fmt);
        if (c != '%') {
            sram_put_byte(&dest[pos], c);
            pos++;
            fmt++;
            continue;
        }
        fmt++;

        int zero_pad = 0;
        int width = 0;

        while (sram_get_byte(fmt) == '0' || sram_get_byte(fmt) == '-') {
            if (sram_get_byte(fmt) == '0') zero_pad = 1;
            fmt++;
        }
        while (sram_get_byte(fmt) >= '0' && sram_get_byte(fmt) <= '9') {
            width = width * 10 + (sram_get_byte(fmt) - '0');
            fmt++;
        }

        if (sram_get_byte(fmt) == 'l') {
            fmt++;
        }

        char fc = sram_get_byte(fmt);
        switch (fc) {
            case 's': {
                const char *s = va_arg(argptr, const char *);
                put_str(dest, max, &pos, s ? s : "(null)");
                break;
            }
            case 'd':
            case 'i': {
                int v = va_arg(argptr, int);
                put_int(dest, max, &pos, v, width, zero_pad);
                break;
            }
            case 'u': {
                unsigned int v = va_arg(argptr, unsigned int);
                put_uint(dest, max, &pos, v, 10, 0, width, zero_pad);
                break;
            }
            case 'x': {
                unsigned int v = va_arg(argptr, unsigned int);
                put_uint(dest, max, &pos, v, 16, 0, width, zero_pad);
                break;
            }
            case 'X': {
                unsigned int v = va_arg(argptr, unsigned int);
                put_uint(dest, max, &pos, v, 16, 1, width, zero_pad);
                break;
            }
            case 'p': {
                void *p = va_arg(argptr, void *);
                if (pos < destMax - 2) {
                    sram_put_byte(&dest[pos], '0'); pos++;
                    sram_put_byte(&dest[pos], 'x'); pos++;
                }
                put_uint(dest, max, &pos, (unsigned int)(uintptr_t)p, 16, 0, 0, 0);
                break;
            }
            case 'c': {
                int ch = va_arg(argptr, int);
                if (pos < max) {
                    sram_put_byte(&dest[pos], (char)ch);
                    pos++;
                }
                break;
            }
            case '%': {
                if (pos < max) {
                    sram_put_byte(&dest[pos], '%');
                    pos++;
                }
                break;
            }
            case 'l': {
                break;
            }
            default: {
                if (pos < max) {
                    sram_put_byte(&dest[pos], fc);
                    pos++;
                }
                break;
            }
        }
        if (sram_get_byte(fmt)) fmt++;
    }
    sram_put_byte(&dest[pos], '\0');
    return (int)pos;
}

__attribute__((weak)) int memset_s(void *dest, unsigned int destMax, int c, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count && i < destMax; i++) {
        sram_put_byte((char *)dest + i, (uint8_t)c);
    }
    return 0;
}

__attribute__((weak)) int memcpy_s(void *dest, unsigned int destMax, const void *src, unsigned int count)
{
    if (!dest || !src || destMax < count) return -1;
    unsigned int i;
    for (i = 0; i < count; i++) {
        sram_put_byte((char *)dest + i, sram_get_byte((const char *)src + i));
    }
    return 0;
}

VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock)
{
    UINT32 intSave = 0;
    UINT32 i;

    if (isLock == UART_WITH_LOCK) {
        intSave = LOS_IntLock();
    }
    for (i = 0; i < len; i++) {
        while ((UART0_LINE_STATUS & UART_LS_THR_EMPTY) == 0) {}
        UART0_DATA = (uint32_t)sram_get_byte(&s[i]);
    }
    if (isLock == UART_WITH_LOCK) {
        LOS_IntRestore(intSave);
    }
}

/*
 * Read one character from UART0 (non-blocking).
 * Returns the byte if data is ready, or -1 if no data available.
 * Non-blocking design allows the shell task to yield CPU via LOS_TaskDelay
 * when no input is pending, preventing watchdog starvation.
 */
INT32 UartGetc(VOID)
{
    if ((UART0_LINE_STATUS & UART_LS_RX_READY) == 0) {
        return -1;
    }
    return (INT32)(UART0_DATA & 0xFF);
}

void ws63_board_init(void)
{
    /* Enable UART FIFO for reliable multi-byte RX reception.
     * Without FIFO, the 1-byte RX holding register overflows when
     * multiple bytes arrive between shell task polls, losing all
     * but the last byte.
     *
     * fifo_ctl register (offset 0x24, per hal_uart_v151_regs_def.h):
     *   bit 4 (fifo_en):     FIFO Enable
     *   bit 5 (tx_fifo_rst): TX FIFO Reset (pulse)
     *   bit 6 (rx_fifo_rst): RX FIFO Reset (pulse)
     */
    volatile uint32_t *fifo_ctl = (volatile uint32_t *)(UART0_BASE + 0x24);
    *fifo_ctl = (1U << 4) | (1U << 5) | (1U << 6);
    *fifo_ctl = (1U << 4);
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

#ifdef LOSCFG_KERNEL_RAM_PRESET_TINY
static int board_u2a(unsigned v, int base, int upper, char *buf)
{
    char tmp[8];
    int i = 0;

    if (v == 0) {
        buf[0] = '0';
        return 1;
    }
    while (v) {
        int d = (int)(v % (unsigned)base);
        tmp[i++] = (d < 10) ? (char)('0' + d) : (char)((upper ? 'A' : 'a') + d - 10);
        v /= (unsigned)base;
    }
    int n = 0;
    while (i > 0) {
        buf[n++] = tmp[--i];
    }
    return n;
}

static int board_vsnprintf_s(char *strDest, size_t destMax,
                             const char *format, va_list ap)
{
    if (strDest == NULL || destMax == 0) {
        return 0;
    }

    size_t pos = 0;
    const char *f = format;

    while (*f != '\0' && pos < destMax - 1) {
        if (*f != '%') {
            strDest[pos++] = *f++;
            continue;
        }
        f++;

        int left = 0;
        int alt = 0;
        for (;;) {
            if (*f == '-') {
                left = 1;
                f++;
            } else if (*f == '#') {
                alt = 1;
                f++;
            } else if (*f == '0' || *f == ' ' || *f == '+') {
                f++;
            } else {
                break;
            }
        }

        int width = 0;
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f++;
        }
        if (*f == '.') {
            f++;
            while (*f >= '0' && *f <= '9') {
                f++;
            }
        }
        while (*f == 'l' || *f == 'h' || *f == 'z' || *f == 'j' || *f == 't') {
            f++;
        }

        char conv = *f;
        if (conv != '\0') {
            f++;
        }

        char body[16];
        int blen = 0;
        const char *str = body;
        int slen = 0;

        switch (conv) {
            case '%':
                body[0] = '%';
                slen = 1;
                break;
            case 'c':
                body[0] = (char)va_arg(ap, int);
                slen = 1;
                break;
            case 's':
                str = va_arg(ap, const char *);
                if (str == NULL) {
                    str = "(null)";
                }
                while (str[slen]) {
                    slen++;
                }
                break;
            case 'd':
            case 'i': {
                int v = va_arg(ap, int);
                unsigned u;
                if (v < 0) {
                    body[blen++] = '-';
                    u = (unsigned)(-v);
                } else {
                    u = (unsigned)v;
                }
                blen += board_u2a(u, 10, 0, body + blen);
                slen = blen;
                break;
            }
            case 'u':
                blen = board_u2a(va_arg(ap, unsigned), 10, 0, body);
                slen = blen;
                break;
            case 'x':
            case 'X': {
                unsigned v = va_arg(ap, unsigned);
                if (alt) {
                    body[blen++] = '0';
                    body[blen++] = (conv == 'X') ? 'X' : 'x';
                }
                blen += board_u2a(v, 16, conv == 'X', body + blen);
                slen = blen;
                break;
            }
            case 'p': {
                unsigned v = (unsigned)(uintptr_t)va_arg(ap, void *);
                body[blen++] = '0';
                body[blen++] = 'x';
                blen += board_u2a(v, 16, 0, body + blen);
                slen = blen;
                break;
            }
            case 'o': {
                unsigned v = va_arg(ap, unsigned);
                if (alt && v != 0) {
                    body[blen++] = '0';
                }
                blen += board_u2a(v, 8, 0, body + blen);
                slen = blen;
                break;
            }
            default:
                body[0] = '%';
                slen = 1;
                if (conv) {
                    body[1] = conv;
                    slen = 2;
                }
                break;
        }

        int pad = width - slen;
        if (pad < 0) {
            pad = 0;
        }
        if (!left) {
            while (pad > 0 && pos < destMax - 1) {
                strDest[pos++] = ' ';
                pad--;
            }
        }
        for (int i = 0; i < slen && pos < destMax - 1; i++) {
            strDest[pos++] = str[i];
        }
        if (left) {
            while (pad > 0 && pos < destMax - 1) {
                strDest[pos++] = ' ';
                pad--;
            }
        }
    }

    strDest[pos] = '\0';
    return (int)pos;
}

static int board_vprintf_to_uart(const char *fmt, va_list ap)
{
    int len = board_vsnprintf_s(g_board_printf_buf, BOARD_PRINTF_BUF_SIZE, fmt, ap);
    if (len > 0) {
        if ((unsigned int)len > BOARD_PRINTF_BUF_SIZE) {
            len = BOARD_PRINTF_BUF_SIZE;
        }
        UartPuts(g_board_printf_buf, (UINT32)len, UART_WITHOUT_LOCK);
    }
    return len;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int len = board_vprintf_to_uart(fmt, ap);
    va_end(ap);
    return len;
}

int vsprintf(char *str, const char *fmt, va_list ap)
{
    return board_vsnprintf_s(str, BOARD_SPRINTF_MAX, fmt, ap);
}
#endif /* LOSCFG_KERNEL_RAM_PRESET_TINY */

#ifdef LOSCFG_SHELL
/*
 * Shell input poll task: checks UART RX status register and writes
 * g_shellInputEvent to wake up ShellTaskEntry (in shmsg.c).
 * ShellTaskEntry then calls UartGetc() to read the actual data.
 */
static VOID ShellInputPollTask(VOID)
{
    while (1) {
        if (UART0_LINE_STATUS & UART_LS_RX_READY) {
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
    task.uwStackSize  = 0x800;
    task.pcName       = "ShellInputPoll";
    task.usTaskPrio   = 9;
    (VOID)LOS_TaskCreate(&taskId, &task);
}
#endif
