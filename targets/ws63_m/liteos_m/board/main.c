/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: WS63 liteos_m entry point (adapted from 2.08.5 main.c)
 *
 * Changes from 2.08.5:
 *   - OsSetMainTask/OsCurrTaskSet/OsMain/OsStart -> LOS_KernelInit + LOS_Start
 *   - LOS_IdleHandlerHookReg -> idle hook via OsIdleLoopHook (if supported)
 *   - los_typedef/los_printf/los_task_pri -> los_config/los_task/los_debug
 */

#include "los_compiler.h"
#include "los_config.h"
#include "los_task.h"
#include "los_debug.h"
#include "los_interrupt.h"
#include "cmsis_os2.h"
#include "soc.h"
#include "pmp_init.h"
#include "riscv_hal.h"
#ifdef LOSCFG_ARCH_CACHE
#include "arch/cache.h"
#endif

#ifdef LOSCFG_SHELL
#include "show.h"
#include "shmsg.h"
#ifdef LOSCFG_SHELL_LK
#include "shell_lk.h"
#endif
#ifdef LOSCFG_SHELL_DMESG
#include "dmesg_pri.h"
#endif
#endif

extern void ws63_board_init(void);
extern void ws63_watchdog_init(uint32_t timeout_seconds, uint8_t mode);
extern void ws63_watchdog_feed(void);

#if defined(LOSCFG_TEST)
extern unsigned int LosAppInit(VOID);
#endif

#define WDT_TIMEOUT_SEC 60

static VOID *app_task(UINT32 arg)
{
    (VOID)arg;
#if !defined(LOSCFG_TEST)
    UINT32 tick = 0;
#endif
    while (1) {
        LOS_TaskDelay(100);
        ws63_watchdog_feed();
#if !defined(LOSCFG_TEST)
        tick++;
#endif
    }
    return NULL;
}

LITE_OS_SEC_TEXT_INIT INT32 main(VOID)
{
    UINT32 ret;

    /* 正式配置：把 OHOS SRAM 配置为 NORMAL (WB+RALLOC) 属性，lock=true
     * 解决 SRAM 默认 DEVICE 属性导致非对齐访问 trap 的问题。
     * 详见 pmp_init.c 与 初始化pmp解析.md §11。
     */
    pmp_init();

#ifdef LOSCFG_ARCH_CACHE
    /* Cache init: flush ICache + DCache before enable, matching fbb_ws63
     * cpu_cache_init. Use ArchDCacheFlush() (writeback+invalidate, safe for
     * stack) instead of ArchDCacheInvalidate() (pure DCIV, no writeback —
     * discards dirty stack saves at -Os). */
    ArchICacheFlush();
    ArchDCacheFlush();
    ArchICacheEnable(CACHE_32KB);
    ArchICachePrefetchEnable(CACHE_PREF_1_LINES);
    ArchDCacheEnable(CACHE_4KB);
#endif

    ws63_board_init();
    dprintf("\nWS63 ENTER MAIN\n");

    ret = osKernelInitialize();
    if (ret != LOS_OK) {
        PRINT_ERR("LOS_KernelInit failed: 0x%x\n", ret);
        while (1) {}
    }

    HalPlicInit();

#ifdef LOSCFG_SHELL
    {
#ifdef LOSCFG_SHELL_LK
        OsLkLoggerInit("shell");
#endif
#ifdef LOSCFG_SHELL_DMESG
        (VOID)OsDmesgInit();
#endif
        ret = LosShellInit();
        if (ret != LOS_OK) {
            PRINT_ERR("Shell init failed: 0x%x\n", ret);
        }
        ShellInputPollTaskInit();
    }
#endif

#if defined(LOSCFG_TEST)
    ret = LosAppInit();
    if (ret != LOS_OK) {
        PRINT_ERR("LosAppInit failed: 0x%x\n", ret);
    }
#endif

    TSK_INIT_PARAM_S param = {0};
    param.pfnTaskEntry = app_task;
    param.usTaskPrio = 20;
    param.uwStackSize = 0x1000;
    param.pcName = "app_task";
    param.uwResved = 0;
    UINT32 taskId;

    ret = LOS_TaskCreate(&taskId, &param);
    if (ret != LOS_OK) {
        PRINT_ERR("LOS_TaskCreate failed: 0x%x\n", ret);
        while (1) {}
    }

    ws63_watchdog_init(WDT_TIMEOUT_SEC, 0);

    osKernelStart();

    while (1) {}
    return 0;
}
