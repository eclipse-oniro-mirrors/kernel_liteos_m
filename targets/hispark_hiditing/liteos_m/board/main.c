/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 liteos_m entry point
 */

#include "los_compiler.h"
#include "los_config.h"
#include "los_task.h"
#include "los_debug.h"
#include "los_interrupt.h"
#include "los_memory.h"
#include "los_ld_elflib.h"
#include "cmsis_os2.h"
#include "soc.h"
#include "pmp_init.h"
#include "riscv_hal.h"
#include "board.h"
#include "watchdog.h"
#include "dmesg_pri.h"
#ifdef LOSCFG_ARCH_CACHE
#include "arch/cache.h"
#endif

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

#ifdef LOSCFG_KERNEL_DYNLOAD
    /* Register the DSO pool with the loader (needs the scheduler up). */
    if (!LOS_DynMemPoolSet((VOID *)HI3322_DYNLOAD_POOL_BASE)) {
        PRINT_ERR("dynload pool set failed\n");
    } else {
        PRINTK("dynload pool ok: 0x%x/0x%x\n", HI3322_DYNLOAD_POOL_BASE, HI3322_DYNLOAD_POOL_SIZE);
    }
#endif

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

/* HI3322 console UART (UART2 "L0" @ 0x5200B000, DW/V151 IP):
 * CTL@0x08 (bit0=div_en, bits2-3=dlen), DIV_H@0x0C, DIV_L@0x10,
 * DIV_FRA@0x14 (6-bit, value/64). 32MHz / (16 * (2 + 43/64)) = 748538Hz. */
#define UART_CTL_REG       (*(volatile UINT32 *)(UART0_BASE + 0x08))
#define UART_DIV_H_REG     (*(volatile UINT32 *)(UART0_BASE + 0x0C))
#define UART_DIV_L_REG     (*(volatile UINT32 *)(UART0_BASE + 0x10))
#define UART_DIV_FRA_REG   (*(volatile UINT32 *)(UART0_BASE + 0x14))

static VOID ConsoleBaudSwitch750000(VOID)
{
    volatile UINT32 wait = 0;

    /* Divisor writes are silently dropped while the UART is busy:
     * wait for TX FIFO empty plus a shift-register drain margin. */
    while ((UART0_FIFO_STATUS & (1U << 1)) == 0) { /* tx_fifo_empty */
        if (wait++ >= 0x1000000) { /* bounded: degrade to no-op, not hang */
            return;
        }
    }
    wait = 0x8000; /* ~2ms @32MHz; one 115200 byte is 87us */
    while (wait--) {
        __asm volatile ("nop");
    }

    UINT32 ctl = UART_CTL_REG;
    UART_CTL_REG = ctl | 0x01U;            /* div_en=1 */
    UART_DIV_H_REG = 0x00;
    UART_DIV_L_REG = 0x02;
    UART_DIV_FRA_REG = 43;                 /* 2 + 43/64 -> 748538Hz */
    UART_CTL_REG = ctl & ~(UINT32)0x01U;   /* div_en=0, restore */
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

    /* Switch the console to 750000: boot-stage lines above stay at 115200
     * (garbled on a 750000 host — expected); BAUDSWITCH marks the boundary. */
    PRINTK("BAUDSWITCH:750000\n");
    ConsoleBaudSwitch750000();
    PRINTK("main: console baud switched to 750000\n");

    pmp_init();
    PRINTK("main: pmp_init ok\n");

#ifdef LOSCFG_ARCH_CACHE
    /* Cache init: flush ICache + DCache before enable, matching fbb 3322
     * OsCacheInit. Use ArchDCacheFlush() (writeback+invalidate, safe for
     * stack) instead of ArchDCacheInvAll() (pure DCIV, no writeback). */
    ArchICacheFlush();
    ArchDCacheFlush();
    (VOID)ArchICacheEnable(CACHE_64KB);
    (VOID)ArchICachePrefetchEnable(CACHE_PREF_1_LINES);
    (VOID)ArchDCacheEnable(CACHE_64KB);
    (VOID)ArchDCachePrefetchEnable(CACHE_PREF_1_LINES, CACHE_PREF_1_LINES);
    PRINTK("main: cache_init ok\n");
#endif

    hi3322_board_init();

    g_sys_mem_addr_end = SYS_MEM_BASE + SYS_MEM_SIZE_DEFAULT;
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

#ifdef LOSCFG_KERNEL_DYNLOAD
    /* DSO load pool: 64KB ITCM window (pmp_init.h); registered in app_task. */
    ret = LOS_MemInit((VOID *)HI3322_DYNLOAD_POOL_BASE, HI3322_DYNLOAD_POOL_SIZE);
    if (ret != LOS_OK) {
        PRINT_ERR("dynload pool init failed: 0x%x\n", ret);
    } else {
        /* Unlink the private pool from the global multipool list. */
        (VOID)LOS_MemDeInit((VOID *)HI3322_DYNLOAD_POOL_BASE);
    }
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
