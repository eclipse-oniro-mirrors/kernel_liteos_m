/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * RK3588 AMP main entry (liteos_m, AArch64 EL1, CPU7).
 *
 * Boot flow (MICA deployment): Linux loads this ELF via remoteproc,
 * PSCI CPU_ON brings CPU3 to arm64_entry (reset_vector.S), which sets up
 * EL1/SP/MMU and jumps here. The flow follows the seven-stage pattern:
 *   board_init -> LOS_KernelInit (GICv3+timer inside ArchInit/tick init)
 *   -> shell -> mica/rpmsg task (LOSCFG_MCS) -> app tasks -> LOS_Start
 */

#include "los_compiler.h"
#include "los_config.h"
#include "los_task.h"
#include "los_debug.h"
#include "los_interrupt.h"
#include "soc.h"
#include "uart.h"
#include "securec.h"
#ifdef LOSCFG_MCS
#include <mica/mica.h>
#include <mica/service.h>
#include <mica_cfg.h>
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

#if defined(LOSCFG_TEST)
extern unsigned int LosAppInit(VOID);
#endif

extern VOID board_init(VOID);
extern VOID ShellInputPollTaskInit(VOID);

STATIC VOID *app_task(UINT32 arg)
{
    (VOID)arg;
    PRINTK("[app] task enter\n");
#if defined(LOSCFG_RK3588_UART) && defined(LOSCFG_SHELL)
    /* osCmdReg() pends the shell command mutex on entry, which reads the
     * current task pointer (TPIDR_EL1) - NULL before LOS_Start(). The
     * uart_test registration is therefore deferred to the first app task
     * run (task context, scheduler up). */
    {
        extern UINT32 UartShellCmdRegister(VOID);
        UINT32 ret = UartShellCmdRegister();
        if (ret != LOS_OK) {
            PRINT_ERR("[app] uart_test register failed: 0x%x\n", ret);
        } else {
            PRINTK("[app] uart_test ready\n");
        }
    }
#endif
    /* placeholder for the AMP application: sleep forever */
    while (1) {
        LOS_TaskDelay(100); /* 100 ticks = 1s */
    }
    return NULL;
}

STATIC UINT32 CreateAppTask(VOID)
{
    UINT32 taskId;
    TSK_INIT_PARAM_S param = {0};

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)app_task;
    param.usTaskPrio = 20;
    param.uwStackSize = 0x1000;
    param.pcName = "app_task";
    param.uwResved = 0;

    return LOS_TaskCreate(&taskId, &param);
}

#ifdef LOSCFG_MCS

#define PSCI_FN_CPU_OFF 0x84000002UL

/* Shared with the exception path (arch/arm64/los_fault.c): after the
 * exception dump the slave powers itself off so the master can restart it
 * (a half-dead slave would stay "ON" and block the next mica_start). */
VOID SlavePoweroff(VOID)
{
    PRINTK("[mica] slave poweroff: PSCI CPU_OFF\n");
    __asm__ volatile (
        "dsb  sy\n"
        "mov  x0, %0\n"
        "smc  #0\n"
        :
        : "r"((UINT64)PSCI_FN_CPU_OFF)
        : "x0", "memory");
    /* PSCI CPU_OFF does not return on success; park just in case. */
    for (;;) {
        __asm__ volatile ("wfe");
    }
}

/* --------------------------------------------------------------------------
 * mica tty shell: the master-side mica console delivers input char by char
 * over the rpmsg tty service (tty_service.c calls shell_cmd_handler per
 * byte). Buffer a line and run it through the same LiteOS shell entry the
 * UART3 console uses (ExecCmdline, shmsg.c). Command output goes to the
 * slave console; the master sees the echo/mica_log traffic.
 * ------------------------------------------------------------------------ */
#define TTY_SHELL_CMD_MAX_LEN 256

/* Shell commands run in a dedicated LOW-priority task (below the mica
 * service threads' pthread default prio 10): ExecCmdline used to run
 * synchronously in the tty service thread, and any command longer than
 * the master demo's rpmsg keep-alive round trip (~hundreds of ms) then
 * starved the echo service until the demo declared -110 and powered the
 * slave core off mid-command. Handing the line to a prio-20 task keeps
 * the rpmsg services responsive for as long as a command runs. */
#define TTY_SHELL_EXEC_PRIO   20
#define TTY_SHELL_EXEC_STACK  0x2000

STATIC CHAR g_ttyCmdBuf[TTY_SHELL_CMD_MAX_LEN];
STATIC UINT32 g_ttyCmdLen;
STATIC CHAR g_ttyExecBuf[TTY_SHELL_CMD_MAX_LEN];
STATIC UINT32 g_ttyExecSem;
STATIC volatile UINT32 g_ttyExecSeqIn;
STATIC volatile UINT32 g_ttyExecSeqOut;

STATIC VOID TtyShellExecTask(UINT32 arg)
{
    (VOID)arg;
    for (;;) {
        (VOID)LOS_SemPend(g_ttyExecSem, LOS_WAIT_FOREVER);
        (VOID)ExecCmdline(g_ttyExecBuf);
        g_ttyExecSeqOut++;
    }
}

STATIC UINT32 TtyShellExecInit(VOID)
{
    TSK_INIT_PARAM_S param = {0};
    UINT32 taskId;

    if (LOS_SemCreate(0, &g_ttyExecSem) != LOS_OK) {
        return LOS_NOK;
    }
    param.pfnTaskEntry = (TSK_ENTRY_FUNC)TtyShellExecTask;
    param.usTaskPrio = TTY_SHELL_EXEC_PRIO;
    param.uwStackSize = TTY_SHELL_EXEC_STACK;
    param.pcName = "TtyShellExec";
    return LOS_TaskCreate(&taskId, &param);
}

STATIC VOID TtyShellHandler(char c)
{
#ifdef LOSCFG_SHELL
    if (c == '\r' || c == '\n') {
        if (g_ttyCmdLen == 0) {
            return;
        }
        g_ttyCmdBuf[g_ttyCmdLen] = '\0';
        /* the demo sends lines strictly one at a time; wait for the
         * previous command to finish before overwriting the exec buffer */
        UINT32 retries = 0;
        while ((g_ttyExecSeqIn != g_ttyExecSeqOut) && (retries++ < 500)) {
            (VOID)LOS_TaskDelay(2); /* bounded 1 s safety net */
        }
        if (memcpy_s(g_ttyExecBuf, sizeof(g_ttyExecBuf),
                     g_ttyCmdBuf, g_ttyCmdLen + 1) == EOK) {
            g_ttyExecSeqIn++;
            (VOID)LOS_SemPost(g_ttyExecSem);
        }
        g_ttyCmdLen = 0;
        return;
    }
    if (c == '\b') { /* backspace */
        if (g_ttyCmdLen > 0) {
            g_ttyCmdLen--;
        }
        return;
    }
    if (g_ttyCmdLen < (TTY_SHELL_CMD_MAX_LEN - 1)) {
        g_ttyCmdBuf[g_ttyCmdLen++] = c;
    }
#else
    (VOID)c;
#endif
}

STATIC VOID MicaTaskEntry(VOID)
{
    PRINTK("[mica] task enter\n");
    /* STATIC: the mica pedestal keeps g_ped_mica_config pointing at this
     * struct after mica_init() returns; a stack-local cfg dies with this
     * task and the later system_poweroff/shell_cmd callbacks would read
     * freed stack (that is why the CPU_OFF request was silently ignored). */
    static struct mica_config cfg;

    (VOID)memset_s(&cfg, sizeof(cfg), 0, sizeof(cfg));
    cfg.shm_base_addr = VDEV_START_ADDR;
    cfg.shm_size = SHM_SIZE;
    cfg.ipc_irq_num = 7; /* master mcs_km: virq 8 maps to GIC INTID 7 (see /proc/interrupts "8: ... GICv3 7 Edge MCS IPI") */
    cfg.sys_ops.shell_cmd_handler = TtyShellHandler;
    cfg.sys_ops.system_poweroff = SlavePoweroff;

    /* shell exec task must exist before any tty line can arrive */
    if (TtyShellExecInit() != LOS_OK) {
        PRINT_ERR("[mica] tty shell exec task create failed\n");
    }

    int mret = mica_init(&cfg);
    if (mret != 0) {
        PRINT_ERR("[mica] mica_init failed: %d\n", mret);
        return;
    }

    mret = mica_create_all_services();
    /* First log that goes over rpmsg (falls back to UART until the tty
     * service is actually up): visible on the master core as proof that
     * the rpmsg log channel works. */
    if (mica_tty_printf("[mica] services up ret=%d\n", mret) <= 0) {
        PRINTK("[mica] services up ret=%d (tty not ready)\n", mret);
    }
}

#endif /* LOSCFG_MCS */

/* --------------------------------------------------------------------------
 * Stray-IRQ defensive cleanup (net + can) - see the GICD note below.
 * ------------------------------------------------------------------------ */
#if defined(LOSCFG_MCS_NET) || defined(LOSCFG_MCS_CAN)

/* GICv3 distributor on RK3588 is at 0xFE600000 and ALWAYS on (driven
 * from the always-on domain), so clearing SPI enable/pending there is
 * safe at any slave boot, regardless of whether the handing-over master
 * ko (mcs_net.ko / mcs_can.ko) is loaded yet.
 *   icenabler = GICD + 0x180 + (INTID/32)*4, bit = INTID%32
 *   icpendr  = GICD + 0x280 + (INTID/32)*4, bit = INTID%32
 *   GMAC1 SPI 234 -> INTID 266: word 8 (0x1a0/0x2a0), bit 10
 *   CAN1  SPI 342 -> INTID 374: word 11 (0x1ac/0x2ac), bit 22
 *   CAN2  SPI 343 -> INTID 375: word 11 (0x1ac/0x2ac), bit 23
 * The peripheral MMIO blocks themselves are NOT touched here: unlike the
 * GICD their clocks depend on the master ko handover (or on Linux clock
 * policy), and a read/write through a gated clock can stall the AXI bus
 * and hang the core - exactly what this cleanup exists to prevent. */
#define GICD_BASE               0xFE600000UL
#define GICD_ICENABLER(n)       ((volatile UINT32 *)(GICD_BASE + 0x180UL + (n) * 4UL))
#define GICD_ICPENDR(n)         ((volatile UINT32 *)(GICD_BASE + 0x280UL + (n) * 4UL))

STATIC VOID ClearStraySpi(UINT32 intid)
{
    UINT32 word = intid / 32u;
    UINT32 bit = 1u << (intid % 32u);

    *GICD_ICENABLER(word) = bit;    /* disable: not forwarded even if the
                                     * level line is still high */
    *GICD_ICPENDR(word) = bit;      /* clear anything latched */
    __asm__ volatile ("dsb sy" ::: "memory");
}
#endif

LITE_OS_SEC_TEXT_INIT INT32 main(VOID)
{
    UINT32 ret;

    /* 1. board: console (UART3) + caches */
    board_init();

    PRINTK("****************************************************\n");
    PRINTK(" RK3588 AMP liteos_m @ CPU7 (AArch64 EL1)\n");
    PRINTK("****************************************************\n");

    /* 2. kernel: memory/GICv3(ArchInit)/tick/task/ipc/swtmr/idle */
    ret = LOS_KernelInit();
    if (ret != LOS_OK) {
        PRINT_ERR("[boot] LOS_KernelInit failed: 0x%x\n", ret);
        while (1) { }
    }
    PRINTK("[boot] kernel init done\n");

#ifdef LOSCFG_MCS_NET
    /* Defensive cleanup of the GMAC1 SPI (INTID 266) before the scheduler
     * enables interrupts. A previous slave run torn down by the master
     * (e.g. the rpmsg demo powering the core off mid-test, before the
     * net demo could run NetStop) leaves the DMA channel status latched
     * RI/NIS with the line enabled in the GICD. The next boot then takes
     * the stray SPI the moment interrupts open - into a vector nobody
     * registered yet - and hangs right after "Entering scheduler"
     * (observed 2026-09-23 on the RK3568 port). Disable + clear-pending
     * at the always-on GICD (see the block comment above for why the
     * GMAC MMIO is deliberately NOT touched on this port); if the level
     * line is still high the pending bit re-latches harmlessly while
     * disabled, and NetStart's ArchHwiCreate (gic-v3 createIrq,
     * per-core ownership) re-establishes the full contract when it
     * runs - the ISR then drains and releases the source. */
    ClearStraySpi(266u);
#endif

#ifdef LOSCFG_MCS_CAN
    /* Same defensive cleanup for the CAN SPIs (INTID 374 CAN1, 375
     * CAN2): a mid-test teardown can leave RX_FINISH pending with the
     * line enabled. GICD-only for the same clock-gating reason. */
    ClearStraySpi(374u);
    ClearStraySpi(375u);
#endif
    /* 3. shell */
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
            PRINT_ERR("[boot] Shell init failed: 0x%x\n", ret);
        }
#if defined(LOSCFG_RK3588_UART) && defined(LOSCFG_SHELL)
        {
            /* uart_test self-test command (board/uart.c, §8.3 of the
             * UART driver plan): liteos_m registers shell commands
             * through osCmdReg, which pends the shell cmd mutex - only
             * legal in task context after the scheduler started. The
             * registration is deferred to the first app_task run; only
             * the mutex-free batch registration (OsShellInit) runs here. */
            (VOID)OsShellInit();
        }
#endif
#ifndef LOSCFG_MCS
        /* MCS AMP mode: the console UART is owned by the Linux master -
         * do NOT poll its RX (would steal master keystrokes); shell input
         * arrives over the rpmsg tty service instead. */
        ShellInputPollTaskInit();
#endif
    }
#endif
    PRINTK("[boot] shell done\n");

    /* 4. mica/rpmsg bring-up runs in the mica task below, after the
     * scheduler started (the openamp_init.c manual path was removed:
     * libmica's pedestal owns metal_init, the notify IRQ and the
     * resource table). */

    /* 5. test suite (debug builds) */
#if defined(LOSCFG_TEST)
    ret = LosAppInit();
    if (ret != LOS_OK) {
        PRINT_ERR("[boot] LosAppInit failed: 0x%x\n", ret);
    }
#endif

    /* 6. app tasks */
    ret = CreateAppTask();
    if (ret != LOS_OK) {
        PRINT_ERR("[boot] CreateAppTask failed: 0x%x\n", ret);
        while (1) { }
    }
    PRINTK("[boot] app task ok\n");

#ifdef LOSCFG_MCS_NET
    /* Real-time NIC user demo (GMAC1 data plane; master mcs_net.ko must
     * have brought the port up first - see net_test.c) */
    {
        extern UINT32 NetTestTaskInit(VOID);
        ret = NetTestTaskInit();
        if (ret != LOS_OK) {
            PRINT_ERR("[boot] NetTestTaskInit failed: 0x%x\n", ret);
        }
        PRINTK("[boot] net task ok\n");
    }
#endif

#ifdef LOSCFG_MCS_CAN
    /* Real-time CAN self-test (CAN1/CAN2 loopback phases, classic +
     * CAN-FD; master mcs_can.ko must have brought the ports up first -
     * see can_test.c) */
    {
        extern UINT32 CanTestTaskInit(VOID);
        ret = CanTestTaskInit();
        if (ret != LOS_OK) {
            PRINT_ERR("[boot] CanTestTaskInit failed: 0x%x\n", ret);
        }
        PRINTK("[boot] can task ok\n");
    }
#endif

    /* 7. mica task (rpmsg bring-up runs once the scheduler is up) */
#ifdef LOSCFG_MCS
    {
        TSK_INIT_PARAM_S micaTaskParam = {0};
        UINT32 micaTaskId;

        micaTaskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)MicaTaskEntry;
        micaTaskParam.pcName = "mica";
        micaTaskParam.usTaskPrio = 26;
        micaTaskParam.uwStackSize = 0x2000;
        ret = LOS_TaskCreate(&micaTaskId, &micaTaskParam);
        if (ret != LOS_OK) {
            PRINT_ERR("create mica task failed, ret=0x%x\n", ret);
        }
        PRINTK("[boot] mica task ok\n");
    }
#endif

    PRINTK("[boot] pre-los-start\n");
    LOS_Start();

    while (1) { }
    return 0;
}
