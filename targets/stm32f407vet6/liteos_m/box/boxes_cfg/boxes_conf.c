#include "boxes_conf.h"
#include "boxes_res_conf.h"
#include "los_box_pri.h"

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