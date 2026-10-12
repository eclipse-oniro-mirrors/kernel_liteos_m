
#include "los_typedef.h"
#include "los_debug.h"
#include "los_config.h"
#include "los_printf.h"
#include "los_spinlock.h"
#include "uart.h"
#include "soc.h"
#include <stdarg.h>
#include "securec.h"

#define UART_DR             (UART0_BASE + 0x00)
#define UART_FR             (UART0_BASE + 0x18)  /* flag register */
#define UART_CR             (UART0_BASE + 0x30)  /* control register */
#define UART_IMSC           (UART0_BASE + 0x38)  /* interrupt mask set/clear */
#define UART_ICR            (UART0_BASE + 0x44)  /* interrupt clear */

#define UART_FR_TXFF        0x20U
#define UART_FR_RXFE        0x10U

static inline volatile UINT8 *Reg8(UINTPTR addr)
{
    return (volatile UINT8 *)addr;
}

VOID UartInit(VOID)
{
    *Reg8(UART_IMSC) = 0x00;
    *Reg8(UART_ICR)  = 0xFF;
}

INT32 UartPutc(INT32 c, VOID *file)
{
    (VOID)file;
    while ((*Reg8(UART_FR) & UART_FR_TXFF) != 0) { }
    *Reg8(UART_DR) = (UINT8)c;
    return c;
}

LITE_OS_SEC_BSS STATIC SPIN_LOCK_INIT(g_uartOutputSpin);

static VOID UartPutsNoLock(const CHAR *s, UINT32 len)
{
    for (UINT32 i = 0; i < len; i++) {

        if (s[i] == '\n') {
            (VOID)UartPutc('\r', NULL);
        }
        (VOID)UartPutc((INT32)s[i], NULL);
    }
}

VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock)
{
    UINT32 intSave;

    if (isLock != UART_WITHOUT_LOCK) {
        LOS_SpinLockSave(&g_uartOutputSpin, &intSave);
        UartPutsNoLock(s, len);
        LOS_SpinUnlockRestore(&g_uartOutputSpin, intSave);
        return;
    }
    UartPutsNoLock(s, len);
}

INT32 UartGetc(VOID)
{
    if ((*Reg8(UART_FR) & UART_FR_RXFE) == 0) {
        return (INT32)(*Reg8(UART_DR));
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
        UartPuts(buf, (UINT32)len, UART_WITH_LOCK);
    }
}
