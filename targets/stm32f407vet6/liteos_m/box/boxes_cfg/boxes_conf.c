#include "boxes_conf.h"
#include "boxes_res_conf.h"
#include "los_box_pri.h"
/* SYS_USR_* layout symbols: was pulled transitively through los_box_pri.h
 * in the demo layer; the header moved to kernel/base (M5A) so the target
 * config file now includes the board memmap directly. */
#include "asm/memmap_config.h"

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