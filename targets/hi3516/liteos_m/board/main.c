#include "los_config.h"
#include "los_debug.h"
#include "los_task.h"

extern UINT32 LosAppInit(VOID);
extern VOID board_init(VOID);
#ifdef LOSCFG_KERNEL_SMP
extern VOID board_start_secondary_core(VOID);
#endif

LITE_OS_SEC_TEXT_INIT INT32 main(VOID)
{
    board_init();

    if (LOS_KernelInit() != LOS_OK) {
        PRINT_ERR("[boot] LOS_KernelInit failed\r\n");
        while (1);
    }

    if (LosAppInit() != LOS_OK) {
        PRINT_ERR("[boot] LosAppInit failed\r\n");
    }

#ifdef LOSCFG_KERNEL_SMP
    board_start_secondary_core();
#endif

    LOS_Start();
    while (1);
}
