#include "boxes_conf.h"
#if defined(LOSCFG_BOX) && (LOSCFG_BASE_IPC_EVENT == 1)
#include "los_event_sid.h"       /* LosEventCB (resource-pool externs, first) */
#endif
#include "boxes_res_conf.h"
#include "los_box_pri.h"
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
#include "los_swtmr.h"           /* LosSwtmrCB types via the public header */
#include "los_swtmr_usr_pri.h"
#endif
/* SYS_USR_* user-world layout symbols: the board memmap owns them, so the
 * target config file includes it directly rather than relying on a kernel
 * header to pull it in transitively. */
#include "asm/memmap_config.h"

#ifdef LOSCFG_KERNEL_TEST
/* UTS test-build box entries.
 *
 *   boxTestEntry   usr_demo/tboxA/boxA_test_main.c - boxA runs the usr test
 *                  phase (functional + memory suites) through the shared
 *                  iCunit copy.
 *   boxBTestEntry  usr_demo/tboxB/boxB_test_main.c - boxB runs the heap suite.
 *                  boxB used to stay down (NULL entry, OsBoxInit skipped it);
 *                  it is enabled because its otherwise-idle heap is the only
 *                  place a box pool may be destroyed and rebuilt.
 *
 * Both are declared here rather than in a header: boxes_auto.cfg is included
 * into g_userBoxCBArr below, so the declarations must precede that include. */
extern VOID boxTestEntry(VOID);
extern VOID boxBTestEntry(VOID);
#endif

SysMemLayout g_sysMemLayoutArr[OS_BOX_MEM_LAYOUT_NUM] = {
    {
        .sysUsrTextStartAddr = SYS_USR_TEXT_START,
        .sysUsrTextEndAddr = SYS_USR_TEXT_END,
        .sysUsrRodataStartAddr = SYS_USR_RODATA_START,
        .sysUsrRodataEndAddr = SYS_USR_RODATA_END,
        .sysUsrDataStartAddr = SYS_USR_DATA_START,
        .sysUsrDataEndAddr = SYS_USR_DATA_END
    }    
};

LosBoxCB g_userBoxCBArr[LOSCFG_BASE_BOX_MAX_LIMIT] = {

#include "boxes_auto.cfg"
};