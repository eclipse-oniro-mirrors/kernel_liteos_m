/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 UART porting (mirror WS63 uart_porting.c)
 *
 * Provides UartPuts, sram_put_byte/get_byte, HalConsoleOutput.
 *
 * NOTE: securec (libboundscheck) already provides strong definitions of
 * vsnprintf_s / memset_s / memcpy_s and is linked into OHOS_Image. An
 * earlier version of this file declared weak overrides of those three
 * functions as a fallback "in case securec gets excluded"; measurements
 * showed securec was always linked so the weak overrides were dead code
 * (confirmed by llvm-nm: final image has the securec versions as `t`,
 * weak overrides never applied). Removed to avoid drift and the known
 * %lld / %p bugs in the hand-rolled vsnprintf_s.
 *
 * Original version used word-aligned read-modify-write for byte access
 * (sram_put_byte/sram_get_byte). That triggers traps on certain DTCM/flash
 * addresses (likely because word read crosses a PMP sub-region boundary or
 * hits a read-only region). hs-fbb uses plain byte accesses via musl's
 * vsnprintf + standard libc memset, with no such issue. Switched here to
 * direct byte pointer access to match hs-fbb behavior.
 */

#include "los_compiler.h"
#include "los_debug.h"
#include "soc.h"
#include "board.h"
#include <stdarg.h>

void sram_put_byte(void *addr, uint8_t c)
{
    *(volatile uint8_t *)addr = c;
}

uint8_t sram_get_byte(const void *addr)
{
    return *(const volatile uint8_t *)addr;
}

VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock)
{
    (VOID)isLock;
    UINT32 i;
    for (i = 0; i < len; i++) {
        while (UART0_FIFO_STATUS & UART_TX_FIFO_FULL) {}
        UART0_DATA = (uint32_t)sram_get_byte(&s[i]);
    }
}

void hi3322_board_init(void)
{
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
        UartPuts(buf, len, FALSE);
    }
}
