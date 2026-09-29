/*
 * Hi3516DV300 board support (liteos_m, Cortex-A7).
 * board_init: console (UART0 PL011) init. Clock is fixed at reset by U-Boot
 * (CNTFRQ set in los_timer.c), heap is initialized by OsMemSystemInit.
 */
#include "los_typedef.h"
#include "uart.h"

VOID board_init(VOID)
{
    UartInit();
}
