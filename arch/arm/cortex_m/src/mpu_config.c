#include "los_typedef.h"
#include "los_task_pri.h"
#include "arch/mpu.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MPU_BIT_SIZE    (30)
#define MPU_CAL_SIZE(size)   ((UINT8)MPU_BIT_SIZE - (UINT8)CLZ(size))

VOID OsBootMpuConfig(VOID);
VOID OsConfigDrvBox(const SysMemLayout *drvBoxMemLayout);
VOID OsRestoreUsrBox(const BoxMemLayout *layout, const SysMemLayout *drvBoxMemLayout);

#ifdef LOSCFG_BASE_IPC_SHMEM
VOID OsSetShMemRegion(UINTPTR startAddr, UINTPTR endAddr);
VOID OsResetShMemRegion(VOID);
#endif
STATIC VOID OsMpuDisable(VOID)
{
    /* Make sure outstanding transfers are done */
    __DMB();
    /* Disable fault exceptions */
    SCB->SHCSR &= ~((UINT32)SCB_SHCSR_MEMFAULTENA_Msk);

    MPU->CTRL = MPU_DISABLE;
}

/**/
STATIC VOID OsMpuEnable(BOOL hfnmiena)
{
    UINT32 enable;

    enable = (UINT32)MPU_CTRL_ENABLE_Msk |
             (UINT32)MPU_CTRL_PRIVDEFENA_Msk |
             ((UINT32)hfnmiena << MPU_CTRL_HFNMIENA_Pos);
    MPU->CTRL = enable;

    /* enable MemManage fault */
    SCB->SHCSR |= (UINT32)SCB_SHCSR_MEMFAULTENA_Msk;
    __DSB();
    __ISB();             
}

STATIC VOID MpuRegionConfig(const MPU_REGION_INFO *mpuInfo, volatile UINT32 *rbar, volatile UINT32 *rasr)
{
    *rbar = mpuInfo->baseAddress | MPU_RBAR_VALID_Msk | mpuInfo->number;
    *rasr = ((UINT32)mpuInfo->accessPermission << MPU_RASR_AP_Pos) |
            ((UINT32)mpuInfo->sharable         << MPU_RASR_S_Pos) |
            ((UINT32)mpuInfo->cacheable        << MPU_RASR_C_Pos) |
            ((UINT32)mpuInfo->buffable         << MPU_RASR_B_Pos) |
            ((UINT32)mpuInfo->xn               << MPU_RASR_XN_Pos) |
            ((UINT32)mpuInfo->regionSize       << MPU_RASR_SIZE_Pos) |
            ((UINT32)MPU_RASR_ENABLE_Msk);  
}

#ifdef LOSCFG_BASE_IPC_SHMEM
STATIC VOID MpuRegionResetConfig(const MPU_REGION_INFO *mpuInfo, volatile UINT32 *rbar, volatile UINT32 *rasr)
{
    *rbar = MPU_RBAR_VALID_Msk | mpuInfo->number;
    *rasr = 0x00;
}

VOID OsSetShMemRegion(UINTPTR startAddr, UINTPTR endAddr)
{
    MPU_REGION_INFO mpuInfo = {0};
    mpuInfo.number = MPU_REGION_NUMBER6;
    mpuInfo.baseAddress = startAddr;
    mpuInfo.regionSize = (UINT8)MPU_CAL_SIZE(endAddr - startAddr);
    mpuInfo.accessPermission = MPU_DEFS_RASR_AP_FULL_ACCESS;
    mpuInfo.xn = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpuInfo.sharable = MPU_ACCESS_NOT_SHAREABLE;
    mpuInfo.cacheable = MPU_ACCESS_CACHEABLE;
    mpuInfo.buffable = MPU_ACCESS_NOT_BUFFERABLE;
    MpuRegionConfig(&mpuInfo, &MPU->RBAR, &MPU->RASR);
}

VOID OsResetShMemRegion(VOID)
{
    MPU_REGION_INFO mpuInfo = {0};
    mpuInfo.number = MPU_REGION_NUMBER6;
    MpuRegionResetConfig(&mpuInfo, &MPU->RBAR, &MPU->RASR);
}
#endif

STATIC INLINE VOID UsrTextMpuConfig(VOID)
{
    /* User text section in Flash */
    MPU_REGION_INFO mpuInfo = {0};
    mpuInfo.number = MPU_REGION_NUMBER0;
    mpuInfo.baseAddress = USR_TEXT_START_ADDR;
    mpuInfo.regionSize = MPU_REGION_SIZE_512KB;
    mpuInfo.accessPermission = MPU_DEFS_RASR_AP_RO;
    mpuInfo.xn = MPU_INSTRUCTION_ACCESS_ENABLE;
    mpuInfo.sharable = MPU_ACCESS_NOT_SHAREABLE;
    mpuInfo.cacheable = MPU_ACCESS_CACHEABLE;
    mpuInfo.buffable = MPU_ACCESS_NOT_BUFFERABLE;
    MpuRegionConfig(&mpuInfo, &MPU->RBAR, &MPU->RASR);
}

STATIC INLINE VOID UsrDataMpuConfig(VOID)
{
    /* User data section in SRAM */
    MPU_REGION_INFO mpuInfo = {0};
    mpuInfo.number = MPU_REGION_NUMBER1;
    mpuInfo.baseAddress = USR_DATA_START;
    mpuInfo.regionSize = MPU_REGION_SIZE_128KB;
    mpuInfo.accessPermission = MPU_DEFS_RASR_AP_FULL_ACCESS;
    mpuInfo.xn = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpuInfo.sharable = MPU_ACCESS_NOT_SHAREABLE;
    mpuInfo.cacheable = MPU_ACCESS_CACHEABLE;
    mpuInfo.buffable = MPU_ACCESS_BUFFERABLE;
    MpuRegionConfig(&mpuInfo, &MPU->RBAR, &MPU->RASR);
}

STATIC INLINE VOID UsrRodataMpuConfig(VOID)
{
    /* User rodata section in Flash */
    MPU_REGION_INFO mpuInfo = {0};
    mpuInfo.number = MPU_REGION_NUMBER7;
    mpuInfo.baseAddress = USR_RODATA_START;
    mpuInfo.regionSize = MPU_REGION_SIZE_1KB;
    mpuInfo.accessPermission = MPU_DEFS_RASR_AP_PRIV_RW_USER_RO;
    mpuInfo.xn = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpuInfo.sharable = MPU_ACCESS_NOT_SHAREABLE;
    mpuInfo.cacheable = MPU_ACCESS_CACHEABLE;
    mpuInfo.buffable = MPU_ACCESS_BUFFERABLE;
    MpuRegionConfig(&mpuInfo, &MPU->RBAR, &MPU->RASR);
}

STATIC INLINE VOID PeriMemMpuConfig(UINT32 accessPermission, volatile UINT32 *rbar, volatile UINT32 *rasr)
{
    MPU_REGION_INFO mpuInfo = {0};
    mpuInfo.number = MPU_REGION_NUMBER2;
    mpuInfo.baseAddress = (UINT32)PERIPH_BASE;
    mpuInfo.regionSize = MPU_REGION_SIZE_512MB;
    mpuInfo.accessPermission = accessPermission;
    mpuInfo.xn = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpuInfo.sharable = MPU_ACCESS_SHAREABLE;
    mpuInfo.cacheable = MPU_ACCESS_NOT_CACHEABLE;
    mpuInfo.buffable = MPU_ACCESS_BUFFERABLE;
    MpuRegionConfig(&mpuInfo, rbar, rasr);
}

STATIC INLINE VOID DrvboxDataMpuConfig(UINT32 accessPermission, volatile UINT32 *rbar, volatile UINT32 *rasr)
{
    MPU_REGION_INFO mpuInfo = {0};
    mpuInfo.number = MPU_REGION_NUMBER3;
    mpuInfo.baseAddress = DRVBX_DATA_START;
    mpuInfo.regionSize = MPU_CAL_SIZE(DRVBX_DATA_END - DRVBX_DATA_START);
    mpuInfo.accessPermission = accessPermission;
    mpuInfo.xn = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpuInfo.sharable = MPU_ACCESS_NOT_SHAREABLE;
    mpuInfo.cacheable = MPU_ACCESS_CACHEABLE;
    mpuInfo.buffable = MPU_ACCESS_BUFFERABLE;
    MpuRegionConfig(&mpuInfo, rbar, rasr);
}

STATIC INLINE VOID BoxDataMpuConfig(UINTPTR startAddr, UINTPTR endAddr, volatile UINT32 *rbar, volatile UINT32 *rasr)
{
    /* Config box data section */
    MPU_REGION_INFO mpuInfo = {0};
    mpuInfo.number = MPU_REGION_NUMBER5;
    mpuInfo.baseAddress = startAddr;
    mpuInfo.regionSize = (UINT8)MPU_CAL_SIZE(endAddr - startAddr);
    mpuInfo.accessPermission = MPU_DEFS_RASR_AP_FULL_ACCESS;
    mpuInfo.xn = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpuInfo.sharable = MPU_ACCESS_NOT_SHAREABLE;
    mpuInfo.cacheable = MPU_ACCESS_CACHEABLE;
    mpuInfo.buffable = MPU_ACCESS_BUFFERABLE;
    MpuRegionConfig(&mpuInfo, rbar, rasr);
}

STATIC INLINE VOID BoxTextMpuConfig(UINTPTR startAddr, UINTPTR endAddr, volatile UINT32 *rbar, volatile UINT32 *rasr)
{
    MPU_REGION_INFO mpuInfo = {0};
    mpuInfo.number = MPU_REGION_NUMBER4;
    mpuInfo.baseAddress = startAddr;
    mpuInfo.regionSize = (UINT8)MPU_CAL_SIZE(endAddr - startAddr);
    mpuInfo.accessPermission = MPU_DEFS_RASR_AP_RO;
    mpuInfo.xn = MPU_INSTRUCTION_ACCESS_ENABLE;
    mpuInfo.sharable = MPU_ACCESS_NOT_SHAREABLE;
    mpuInfo.cacheable = MPU_ACCESS_CACHEABLE;
    mpuInfo.buffable = MPU_ACCESS_NOT_BUFFERABLE;
    MpuRegionConfig(&mpuInfo, rbar, rasr);
}

VOID OsBootMpuConfig(VOID)
{
    /* Disbale MPU */
    OsMpuDisable();

    UsrTextMpuConfig();
    UsrDataMpuConfig();
    UsrRodataMpuConfig();

    /* Enable MPU */
    OsMpuEnable(MPU_HFNMIENA_DISABLE);
}

LITE_OS_SEC_TEXT VOID OsGetMpuConfig(const LosBoxCB *boxCB, MPU_REGION_REGISTER *mpuCfg, UINT32 len)
{
    UINT32 idx = 0;
    const BoxMemLayout *boxMem = &boxCB->layout[0];

    if (len != MPU_CONFIG_NUM_IN_SCH) {
        return;
    }
    if (boxCB->boxId == (UINT16)LOS_DRV_BOX_ID) {
        PeriMemMpuConfig(MPU_DEFS_RASR_AP_FULL_ACCESS, &mpuCfg[idx].rbar, &mpuCfg[idx].rasr);
        idx++;
        DrvboxDataMpuConfig(MPU_DEFS_RASR_AP_FULL_ACCESS, &mpuCfg[idx].rbar, &mpuCfg[idx].rasr);
        idx++;
        BoxTextMpuConfig(boxMem->boxTextStartAddr, boxMem->boxTextEndAddr, &mpuCfg[idx].rbar, &mpuCfg[idx].rasr);
        idx++;
        mpuCfg[idx].rbar = MPU_REGION_NUMBER5 | MPU_RBAR_VALID_Msk;
        mpuCfg[idx].rasr = 0;
#ifdef LOSCFG_BASE_IPC_SHMEM
        idx++;
        mpuCfg[idx].rbar = MPU_REGION_NUMBER6 | MPU_RBAR_VALID_Msk;
        mpuCfg[idx].rasr = 0;
#endif
    } else {
        PeriMemMpuConfig(MPU_DEFS_RASR_AP_PRIV_RW, &mpuCfg[idx].rbar, &mpuCfg[idx].rasr);
        idx++;
        DrvboxDataMpuConfig(MPU_DEFS_RASR_AP_PRIV_RW, &mpuCfg[idx].rbar, &mpuCfg[idx].rasr);
        idx++;
        BoxTextMpuConfig(boxMem->boxTextStartAddr, boxMem->boxTextEndAddr, &mpuCfg[idx].rbar, &mpuCfg[idx].rasr);
        idx++;
        BoxDataMpuConfig(boxMem->boxDataStartAddr, boxMem->boxDataEndAddr, &mpuCfg[idx].rbar, &mpuCfg[idx].rasr);
#ifdef LOSCFG_BASE_IPC_SHMEM
        idx++;
        mpuCfg[idx].rbar = MPU_REGION_NUMBER6 | MPU_RBAR_VALID_Msk;
        mpuCfg[idx].rasr = 0;
#endif
    }
}

LITE_OS_SEC_TEXT VOID OsConfigDrvBox(const SysMemLayout *drvBoxMemLayout)
{
    BoxTextMpuConfig(drvBoxMemLayout[0].textStartAddr, drvBoxMemLayout[0].textEndAddr, &MPU->RBAR, &MPU->RASR);

    DrvboxDataMpuConfig(MPU_DEFS_RASR_AP_FULL_ACCESS, &MPU->RBAR, &MPU->RASR);

    PeriMemMpuConfig(MPU_DEFS_RASR_AP_FULL_ACCESS, &MPU->RBAR, &MPU->RASR);
}

LITE_OS_SEC_TEXT VOID OsRestoreUsrBox(const BoxMemLayout *layout , const SysMemLayout *drvBoxMemLayout)
{
    (VOID)drvBoxMemLayout;

    BoxTextMpuConfig(layout[0].boxTextStartAddr, layout[0].boxTextEndAddr, &MPU->RBAR, &MPU->RASR);

    DrvboxDataMpuConfig(MPU_DEFS_RASR_AP_PRIV_RW, &MPU->RBAR, &MPU->RASR);

    PeriMemMpuConfig(MPU_DEFS_RASR_AP_PRIV_RW, &MPU->RBAR, &MPU->RASR);
}
#ifdef __cplusplus
}
#endif /* __cplusplus */