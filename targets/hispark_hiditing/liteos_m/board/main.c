/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 liteos_m entry point
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
#include "board.h"
#include "watchdog.h"
#include "dmesg_pri.h"

#if defined(LOSCFG_TEST)
extern unsigned int LosAppInit(VOID);
#endif
#ifdef LOSCFG_FS_LITTLEFS
extern int nor_lfs_mount_all(void);
#endif
/* SPI-NAND (DS35M1GA) file system (YAFFS2_BINARY_LIB_PORTING_PLAN.md):
 * fbb-aligned two-phase API — phase "true" initializes the FMC driver and
 * mounts /boot, phase "false" mounts /system and /user. The backend
 * (yaffs closed archive / littlefs) is selected by board_nand_backend. */
extern int nandflash_filesystem_init(bool part_boot);

/*
 * Stack canary value for -fstack-protector-strong.
 * 0x00: null byte terminates string operations
 * 0x0a: LF, 0x0d: CR — prevent string-based attacks
 * 0xff: all-ones byte
 *
 * musl libc already defines __stack_chk_guard (in
 * third_party/musl/porting/liteos_m/kernel/src/env/__stack_chk_fail.c),
 * but on this bare-metal liteos_m port musl's __init_ssp() is never
 * called automatically (no __libc_start_main path) so the symbol stays 0
 * in .bss. Seed it explicitly here before any -fstack-protector-strong
 * function runs. TODO: this should live in the arch layer (see hs-fbb
 * arch_lite/riscv/src/canary.c) instead of board main.c.
 */
#define STACK_CHK_GUARD_VALUE 0x000a0dff
extern UINT32 __stack_chk_guard;

/*
 * Boot watchdog budget: 60s while the NAND mounts + smoke run, 10s in
 * steady state (re-armed at the end of app_task below).
 *
 * R3 root cause (R4 trace, plan section 14): the first mount after a power
 * cut walks the yaffs2_scan_backwards "partially written block" path —
 * minutes-long, dominated by in-memory chunk-bitmap/object-tree repair with
 * page-read kicks only a small fraction of the time. A single 10s window
 * starves mid-scan (R3 reset 13.2s after phase 1 completed, no output); the
 * trace lines "Partially written block N detected. gc will fix this." name
 * the exact path. Once mounted, yaffs_sync stores a checkpoint and later
 * mounts take the fast restore path, so the wide budget only pays on the
 * first boot after an unclean power-off.
 */
#define WDT_TIMEOUT_BOOT_SEC 60
#define WDT_TIMEOUT_RUN_SEC 10

static VOID *app_task(UINT32 arg)
{
    (VOID)arg;

#if !defined(LOSCFG_TEST)
    /* File-system bring-up and the tight 10s watchdog re-arm run only on the
     * normal product. The kernel test product (hi3322_testsuits) has no
     * yaffs/fs test cases: skipping the mounts keeps the yaffs pool and the
     * smoke-test buffers out of DTCM and the littlefs mount buffers out of
     * the kernel heap. LOSCFG_TEST comes from the KERNEL_TEST kconfig chain
     * (the same guard that pulls in LosAppInit below); the test build keeps
     * the 60s boot watchdog window since pressure cases can starve this
     * task. */
    UINT32 ret;

#ifdef LOSCFG_FS_LITTLEFS
    ret = nor_lfs_mount_all();
    PRINTK("nor_lfs_mount_all ret=0x%x\n", ret);
#endif

    /* SPI-NAND file system: two-phase fbb-aligned init (see the extern
     * declaration above). The single-task flow just runs both phases back
     * to back; the two calls preserve the fbb nandflash_filesystem_init
     * (bool) signature. */
    ret = nandflash_filesystem_init(true);
    PRINTK("nandflash_filesystem_init(boot) ret=0x%x\n", ret);
    ret = nandflash_filesystem_init(false);
    PRINTK("nandflash_filesystem_init(rest) ret=0x%x\n", ret);

#if defined(ENABLE_NAND_SMOKE_TEST)
    /* NAND smoke test (backend-agnostic POSIX/VFS level, morpheus-proven):
     * TC-rw/TC-big/TC-dir/TC-statfs/TC-persist per volume. */
    {
        extern int nand_smoke_test_run(void);
        ret = nand_smoke_test_run();
        PRINTK("nand_smoke_test failed_cases=0x%x\n", ret);
    }
#endif

#if defined(BOARD_NAND_BACKEND_YAFFS)
    /* Store a checkpoint of every mounted volume: the NEXT mount takes the
     * fast checkpoint-restore path instead of the minute-long full scan
     * through partially-written blocks (R3/R4 lesson, plan section 14). */
    {
        extern void nandflash_yaffs_sync(void);
        nandflash_yaffs_sync();
        PRINTK("nandflash_yaffs_sync done\n");
    }
#endif

    /* Steady state: tighten the watchdog back to the normal window. */
    hi3322_watchdog_init(WDT_TIMEOUT_RUN_SEC, 0);
    hi3322_watchdog_feed();
    PRINTK("wdt re-armed %ds\n", WDT_TIMEOUT_RUN_SEC);
#endif /* !LOSCFG_TEST */

    while (1) {
        LOS_TaskDelay(100);
        hi3322_watchdog_feed();
    }
    return NULL;
}

LITE_OS_SEC_TEXT_INIT INT32 main(VOID)
{
    UINT32 ret;

    /*
     * Initialize stack canary — must happen before any function with
     * -fstack-protector-strong is called (which is virtually everything,
     * including PRINTK → HalConsoleOutput → securec vsnprintf_s).
     */
    __stack_chk_guard = STACK_CHK_GUARD_VALUE;

    /*
     * DEBUG: confirm we reached main(). If this print doesn't appear,
     * the trap happened in reset_vector.S (PMP/FPU/.data copy) — see
     * the early UART marker added there.
     */
    PRINTK("main: enter\n");

    pmp_init();
    PRINTK("main: pmp_init ok\n");

    hi3322_board_init();
    /*
     * Enable the hardware watchdog (reset_vector disabled it during init).
     * Boot budget first (see WDT_TIMEOUT_BOOT_SEC above); app_task re-arms
     * the tight 10s window once the mounts + smoke have settled.
     */
    hi3322_watchdog_init(WDT_TIMEOUT_BOOT_SEC, 0);
    PRINTK("main: wdt_init ok\n");

    PRINTK("HI3322 ENTER MAIN\n");

    HalPlicInit();
    PRINTK("HalPlicInit done\n");

    ret = osKernelInitialize();
    PRINTK("LOS_KernelInit ret=0x%x\n", ret);
    if (ret != LOS_OK) {
        PRINT_ERR("LOS_KernelInit failed: 0x%x\n", ret);
        while (1) {}
    }

#ifdef LOSCFG_SHELL_DMESG
    (VOID)OsDmesgInit();
#endif

#if defined(LOSCFG_TEST)
    ret = LosAppInit();
    PRINTK("LosAppInit ret=0x%x\n", ret);
    if (ret != LOS_OK) {
        PRINT_ERR("LosAppInit failed: 0x%x\n", ret);
    }
#endif

    TSK_INIT_PARAM_S param = {0};
    param.pfnTaskEntry = app_task;
    param.usTaskPrio = 20;
    param.uwStackSize = 0x2000;
    param.pcName = "app_task";
    param.uwResved = 0;
    UINT32 taskId;

    ret = LOS_TaskCreate(&taskId, &param);
    PRINTK("LOS_TaskCreate ret=0x%x\n", ret);
    if (ret != LOS_OK) {
        PRINT_ERR("LOS_TaskCreate failed: 0x%x\n", ret);
        while (1) {}
    }

    PRINTK("LOS_Start...\n");
    osKernelStart();

    while (1) {}
    return 0;
}
