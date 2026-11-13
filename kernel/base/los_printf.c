/* ----------------------------------------------------------------------------
 * Copyright (c) Huawei Device Co., Ltd. 2020-2026. All rights reserved.
 * Description: LiteOS Printf Implementation
 * Author: Huawei LiteOS Team
 * Create: 2013-01-01
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --------------------------------------------------------------------------- */
/* ---------------------------------------------------------------------------
 * Notice of los_printf.c Modification
 *===============================================
 * When this file is getting modified, it should consider the compatibility of SE LiteOS.
 * It should not add OS inner invocation, which is not allowed to be accessed in user space.
 * But it's allowed to invoke syscall interface.
 *---------------------------------------------------------------------------- */

#ifdef LOSCFG_LIB_LIBC
#include "unistd.h"
#endif
#include "los_debug.h"
#include "los_memory.h"
#include "los_task_pri.h"

#ifdef LOSCFG_SHELL_DMESG
#include "dmesg_pri.h"
#endif
#ifdef LOSCFG_SHELL_EXCINFO_DUMP
#include "los_exc_pri.h"
#endif
#include "los_printf_pri.h"

#include <string.h>
#include "securec.h"

/* M5G-R: this whole TU moves to the COMMON sections under BOX (compiled
 * in the box_usr_mem module, kernel/base drops it): box user code calls
 * PRINTK -> LOS_Printf through the same code the kernel uses - the
 * "full PRINTK" for box code. The move required two structural changes:
 *  1. OsLogLevelCheck (+ its STATIC g_logString) moved here from
 *     los_debug.c - it sits on the LOS_Printf call path and would be a
 *     kernel-segment landing point otherwise (pitfall 44: audit every UND
 *     symbol of a COMMON-segment TU). The non-COMMON level prefix still
 *     touches g_logString/LOS_CurTaskNameGet, which user callers never
 *     reach (PRINTK is always LOG_COMMON_LEVEL).
 *  2. the formatting engine: under LOS_PRINTF_COMMON_ENGINE (defined by
 *     the box_usr_mem build) OsVprintf formats with OsVsnprintfCommon
 *     instead of vsnprintf_s. The libsec/musl printf closure (FILE
 *     streams, pthread locks, TLS errno, soft-double conversion) cannot
 *     be routed to the COMMON sections - pitfall 45's "burden members
 *     stay out" at printf scale. The KERNEL build (BOX=off) keeps the
 *     full vsnprintf_s engine, bit-for-bit. */
#if (LOSCFG_KERNEL_PRINTF == 1) || (LOSCFG_KERNEL_PRINTF == 2)
STATIC const CHAR *g_logString[] = {
    "EMG",
    "COMMON",
    "ERR",
    "WARN",
    "INFO",
    "DEBUG",
};

INT32 OsLogLevelCheck(INT32 level)
{
    if (level > PRINT_LEVEL) {
        return (INT32)LOS_NOK;
    }

    if ((level != LOG_COMMON_LEVEL) && ((level > LOG_EMG_LEVEL) && (level <= LOG_DEBUG_LEVEL))) {
        PRINTK("[%s][%s]", g_logString[level], LOS_CurTaskNameGet());
    }

    return LOS_OK;
}
#endif

#define SIZEBUF  256

typedef enum {
    NO_OUTPUT = 0,
    UART_OUTPUT = 1,
    CONSOLE_OUTPUT = 2,
    EXC_OUTPUT = 3
} OutputType;

/* Weak default UartPuts: no-op. Boards provide a strong UartPuts for actual output. */
WEAK VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock)
{
    (VOID)s;
    (VOID)len;
    (VOID)isLock;
}

#ifdef LOS_PRINTF_COMMON_ENGINE
/* COMMON build: reach the kernel-segment uart driver through the
 * UartPuts syscall (it runs privileged, so the driver's LOS_IntLock
 * and register writes are legal there). Kernel build keeps the plain
 * UartPuts call below. The extern restates the COMMON_TEXT veneer
 * prototype (same shape the usr/syscalls.h def expansion declares). */
extern VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock);
#define OS_UART_PUTS(s, len, lock)   UartPuts((s), (len), (lock))
#else
#define OS_UART_PUTS(s, len, lock)   UartPuts((s), (len), (lock))
#endif

STATIC VOID ErrorMsg(VOID)
{
    const CHAR *p = "Output illegal string! vsnprintf_s failed!\n";
    OS_UART_PUTS(p, (UINT32)strlen(p), UART_WITH_LOCK);
}

STATIC VOID UartOutput(const CHAR *str, UINT32 len, BOOL isLock)
{
#ifdef LOSCFG_SHELL_DMESG
    if (!OsCheckUartLock()) {
        OS_UART_PUTS(str, len, isLock);
    }
    if (isLock != UART_WITHOUT_LOCK) {
        (VOID)OsLogMemcpyRecord(str, len);
    }
#else
    OS_UART_PUTS(str, len, isLock);
#endif
}

STATIC VOID OutputControl(const CHAR *str, UINT32 len, OutputType type)
{
    switch (type) {
        case CONSOLE_OUTPUT:
#ifdef LOSCFG_KERNEL_CONSOLE
            if (OsConsoleIsEnabled() == TRUE) {
                (VOID)write(STDOUT_FILENO, str, (size_t)len);
                break;
            }
#endif
            /* fall-through */
        case UART_OUTPUT:
            UartOutput(str, len, UART_WITH_LOCK);
            break;
        case EXC_OUTPUT:
            UartOutput(str, len, UART_WITHOUT_LOCK);
            break;
        default:
            break;
    }
    return;
}

#ifdef LOS_PRINTF_COMMON_ENGINE
STATIC INT32 OsVsnprintfCommonImpl(CHAR *dst, UINT32 dstMax, const CHAR *fmt, va_list ap);
#endif

STATIC VOID OsVprintf(const CHAR *fmt, va_list ap, OutputType type)
{
    INT32 len = 0;
    CHAR aBuf[SIZEBUF] = {0};
    CHAR *bBuf = NULL;
    UINT32 bufLen = SIZEBUF;

    bBuf = aBuf;
#ifdef LOS_PRINTF_COMMON_ENGINE
    len = OsVsnprintfCommonImpl(bBuf, bufLen, fmt, ap);
#else
    len = vsnprintf_s(bBuf, bufLen, bufLen - 1, fmt, ap);
#endif
#ifdef LOSCFG_KERNEL_PRINTF_SIZE_EXTEND
    if ((len == -1) && (*bBuf == '\0')) {
        /* parameter is illegal or some features in fmt dont support */
        ErrorMsg();
        return;
    }

    const CHAR *errMsgMalloc = "OsVprintf, malloc failed!\n";
    while (len == -1) {
        /* bBuf is not enough */
        if (bufLen != SIZEBUF) {
            (VOID)LOS_MemFree(m_aucSysMem0, bBuf);
        }

        bufLen = bufLen << 1;
        bBuf = (CHAR *)LOS_MemAlloc(m_aucSysMem0, bufLen);
        if (bBuf == NULL) {
            UartPuts(errMsgMalloc, (UINT32)strlen(errMsgMalloc), UART_WITH_LOCK);
            return;  /* must return here, otherwise NULL dereference below */
        }

        len = vsnprintf_s(bBuf, bufLen, bufLen - 1, fmt, ap);

        if (*bBuf == '\0') {
            /* parameter is illegal or some features in fmt dont support */
            (VOID)LOS_MemFree(m_aucSysMem0, bBuf);
            ErrorMsg();
            return;
        }
    }
#else
    if (len == -1) {
        /* parameter is illegal or some features in fmt dont support */
        ErrorMsg();
        return;
    }
#endif

    *(bBuf + len) = '\0';
    OutputControl(bBuf, len, type);
#ifdef LOSCFG_KERNEL_PRINTF_SIZE_EXTEND
    if (bufLen != SIZEBUF) {
        (VOID)LOS_MemFree(m_aucSysMem0, bBuf);
    }
#endif
}

VOID UartVprintf(const CHAR *fmt, va_list ap)
{
    OsVprintf(fmt, ap, UART_OUTPUT);
}

VOID ConsoleVprintf(const CHAR *fmt, va_list ap)
{
    OsVprintf(fmt, ap, CONSOLE_OUTPUT);
}

#if (LOSCFG_KERNEL_PRINTF == 1)
VOID LOS_Printf(LogModuleType type, INT32 level, const CHAR *fmt, ...)
{
    (VOID)type;
    if (OsLogLevelCheck(level) != LOS_OK) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    OsVprintf(fmt, ap, CONSOLE_OUTPUT);
    va_end(ap);
}
#endif

#ifdef LOSCFG_LIB_LIBC
#if !defined(LOSCFG_LIBC_NEWLIB)
__attribute__((noinline)) VOID dprintf(const CHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    OsVprintf(fmt, ap, CONSOLE_OUTPUT);
    va_end(ap);
}
#endif /* !LOSCFG_LIBC_NEWLIB */
#endif /* LOSCFG_LIB_LIBC */

VOID LkDprintf(const CHAR *fmt, va_list ap)
{
    OsVprintf(fmt, ap, CONSOLE_OUTPUT);
}

#ifdef LOSCFG_SHELL_DMESG
VOID DmesgPrintf(const CHAR *fmt, va_list ap)
{
    OsVprintf(fmt, ap, CONSOLE_OUTPUT);
}
#endif

VOID PrintExcInfo(const CHAR *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* uart output without print-spinlock */
    OsVprintf(fmt, ap, EXC_OUTPUT);
#ifdef LOSCFG_SHELL_EXCINFO_DUMP
    WriteExcBufVa(fmt, ap);
#endif
    va_end(ap);
}

#ifdef LOS_PRINTF_COMMON_ENGINE
/*
 * Lightweight formatting engine for the COMMON build of this TU (see the
 * header comment). Supports: %s %d %i %u %x %X %p %c %%; length modifiers
 * l/ll/z/h (accepted, ignored - 32-bit values); field width with optional
 * leading-0 padding (%08x style); unknown specs emit verbatim. Returns the
 * number of characters stored (excluding the terminator), or -1 when the
 * buffer is too small - matching the vsnprintf_s error contract OsVprintf
 * checks (SIZE_EXTEND re-format loops are compiled out in this build).
 */
STATIC INT32 OsVsnprintfCommonImpl(CHAR *dst, UINT32 dstMax, const CHAR *fmt, va_list ap)
{
    UINT32 pos = 0;
    const CHAR *p = fmt;

    if ((dst == NULL) || (dstMax == 0)) {
        return -1;
    }
    while (*p != '\0') {
        UINT32 width = 0;
        CHAR pad = ' ';
        CHAR spec;
        CHAR digits[12];
        UINT32 ndig;
        UINT32 i;
        UINT32 ulValue;
        INT32 sValue;
        BOOL negative;
        const CHAR *s;

        if (pos >= (dstMax - 1)) {
            dst[pos] = '\0';
            return -1;
        }
        if (*p != '%') {
            dst[pos++] = *p++;
            continue;
        }
        p++;
        if (*p == '%') {
            dst[pos++] = *p++;
            continue;
        }
        if (*p == '0') {
            pad = '0';
            p++;
        }
        while ((*p >= '0') && (*p <= '9')) {      /* field width */
            width = (width * 10U) + (UINT32)(*p - '0');
            p++;
        }
        while ((*p == 'l') || (*p == 'h') || (*p == 'z')) {
            p++;                                  /* length mods: 32-bit only */
        }
        spec = *p++;
        ulValue = 0;
        negative = FALSE;
        ndig = 0;
        switch (spec) {
            case 'c':
                dst[pos++] = (CHAR)va_arg(ap, INT32);
                continue;
            case 's':
                s = va_arg(ap, const CHAR *);
                if (s == NULL) {
                    s = "(null)";
                }
                while (*s != '\0') {
                    if (pos >= (dstMax - 1)) {
                        dst[pos] = '\0';
                        return -1;
                    }
                    dst[pos++] = *s++;
                }
                continue;
            case 'd':
            case 'i':
                sValue = va_arg(ap, INT32);
                negative = (sValue < 0);
                ulValue = (negative ? (UINT32)(-sValue) : (UINT32)sValue);
                break;
            case 'u':
                ulValue = va_arg(ap, UINT32);
                break;
            case 'p':
                if (pos >= (dstMax - 3)) {
                    dst[pos] = '\0';
                    return -1;
                }
                dst[pos++] = '0';
                dst[pos++] = 'x';
                ulValue = (UINT32)(UINTPTR)va_arg(ap, VOID *);
                width = 0;
                pad = ' ';
                break;
            case 'x':
            case 'X':
                ulValue = va_arg(ap, UINT32);
                break;
            default:                               /* unknown spec: verbatim */
                if (pos >= (dstMax - 2)) {
                    dst[pos] = '\0';
                    return -1;
                }
                dst[pos++] = '%';
                dst[pos++] = spec;
                continue;
        }
        /* emit the numeric conversion for d/i/u/x/X/p */
        if ((spec == 'x') || (spec == 'X') || (spec == 'p')) {
            if (ulValue == 0) {
                digits[ndig++] = '0';
            }
            while (ulValue != 0) {
                UINT32 v = ulValue & 0xFU;
                digits[ndig++] = (CHAR)((v < 10U) ? ('0' + v) :
                                        ((spec == 'x') ? ('a' + v - 10U) : ('A' + v - 10U)));
                ulValue >>= 4;
            }
        } else {
            if (ulValue == 0) {
                digits[ndig++] = '0';
            }
            while (ulValue != 0) {
                digits[ndig++] = (CHAR)('0' + (ulValue % 10U));
                ulValue /= 10U;
            }
            if (negative) {
                digits[ndig++] = '-';
            }
        }
        if ((width != 0) && (width > ndig)) {
            UINT32 padding = width - ndig;
            if (pos + padding + ndig >= (dstMax - 1)) {
                dst[pos] = '\0';
                return -1;
            }
            for (i = 0; i < padding; i++) {
                dst[pos++] = pad;
            }
        }
        if (pos + ndig >= (dstMax - 1)) {
            dst[pos] = '\0';
            return -1;
        }
        while (ndig > 0) {
            ndig--;
            dst[pos++] = digits[ndig];
        }
    }
    dst[pos] = '\0';
    return (INT32)pos;
}
#endif /* LOS_PRINTF_COMMON_ENGINE */
