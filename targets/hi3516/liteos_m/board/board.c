#include "los_typedef.h"
#include "uart.h"

#ifdef LOSCFG_KERNEL_SMP
#include "los_percpu_pri.h"

#define CRG_REG_BASE            0x12010000U
#define PERI_CRG30              (CRG_REG_BASE + 0x78U)
#define CRG30_CPU1_RESET_BIT    (1U << 2)
#define BOOTROM_VIRT_BASE       0xE0000000U

extern VOID reset_vector(VOID);
extern volatile UINT32 g_secondaryOnline;

VOID board_start_secondary_core(VOID)
{
    volatile UINT32 *crg30 = (volatile UINT32 *)PERI_CRG30;
    volatile UINT32 *bootrom = (volatile UINT32 *)BOOTROM_VIRT_BASE;
    UINT32 regval;
    UINT32 timeout = 0;

    bootrom[0] = 0xe51ff004;
    bootrom[1] = (UINT32)(UINTPTR)reset_vector;

    regval = *crg30;
    regval &= ~CRG30_CPU1_RESET_BIT;
    *crg30 = regval;

    while ((g_secondaryOnline == 0) && (timeout < 1000000)) {
        timeout++;
    }
}
#endif

VOID board_init(VOID)
{
    UartInit();
}
