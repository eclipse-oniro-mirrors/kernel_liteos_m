/*
 * Hi3516DV300 main entry (liteos_m, Cortex-A7).
 */
#include "los_config.h"
#include "los_debug.h"
#include "los_task.h"
#include "osTest.h"

extern UINT32 LosAppInit(VOID);
extern VOID board_init(VOID);

LITE_OS_SEC_TEXT_INIT INT32 main(VOID)
{
    board_init();
    PRINTK("[boot] Hi3516DV300 ENTER MAIN\r\n");

    if (LOS_KernelInit() != LOS_OK) {
        PRINT_ERR("[boot] LOS_KernelInit failed\r\n");
        while (1);
    }

    if (LosAppInit() != LOS_OK) {
        PRINT_ERR("[boot] LosAppInit failed\r\n");
    }

    LOS_Start();
    while (1);
}
