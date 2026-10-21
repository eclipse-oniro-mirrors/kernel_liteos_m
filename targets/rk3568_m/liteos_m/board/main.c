/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * RK3568 AMP main entry (liteos_m, AArch64 EL1, CPU3).
 *
 * Boot flow (MICA deployment): Linux loads this ELF via remoteproc,
 * PSCI CPU_ON brings CPU3 to reset_vector (reset_vector.S), which sets up
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
#if defined(LOSCFG_RK3568_UART) && defined(LOSCFG_SHELL)
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
        {
            /* TEMP rpcdiag: ride the proven tick channel (REMOVE BEFORE SUBMIT) */
            extern unsigned int mica_rpc_diag_probe(unsigned int *addr, unsigned int *dst,
                                                   unsigned int *rxCb, unsigned int *noh,
                                                   unsigned int *loopN, unsigned int *rxCnt);
            unsigned int rpcAddr, rpcDst, rxCb, noh, loopN, rxCnt;
            unsigned int rpcReady = mica_rpc_diag_probe(&rpcAddr, &rpcDst, &rxCb, &noh, &loopN, &rxCnt);
            mica_tty_printf("[app] tick alive %u rpc=%u(addr=%x dst=%x cb=%u noh=%u lp=%u rx=%u) tty=%d umt=%d\n",
                           (UINT32)LOS_TickCountGet(), rpcReady, rpcAddr, rpcDst,
                           rxCb, noh, loopN, rxCnt,
                           mica_service_is_ready(MICA_SERVICE_TTY),
                           mica_service_is_ready(MICA_SERVICE_UMT));
        }
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
    /* No PRINTK here on purpose: this runs in the IPI interrupt context and
     * the shared UART is owned by the master console; a log write can wedge
     * (or stall on TX FIFO contention with the master's console traffic),
     * the SMC below never executes, the IPI stays masked, and the master's
     * wait_cpu_off times out leaving the core on - which then gets its image
     * overwritten by the next load. Power-off must be print-free. */
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
        (VOID)mica_tty_printf("[tty] exec: '%s'\n", g_ttyExecBuf);
        (VOID)ExecCmdline(g_ttyExecBuf);
        (VOID)mica_tty_printf("[tty] exec done\n");
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

#if defined(LOSCFG_TEST)
/* MCS_TEST_SGI_DIAG: runtime probe for the master->slave SGI7 IPI.
 * The GIC bring-up code is instruction-identical to the working liteos_mv
 * image, yet the mica receiver never wakes with the test image. This task
 * dumps CPU3's redistributor SGI/PPI state + CPU interface, self-tests
 * the SGI receive chain with SGI6 (unused: Linux arch IPIs never target
 * CPU3), then watches GICR_ISPENDR0[7] while the master retries its kick. */
STATIC volatile UINT32 g_probeCount = 0;

STATIC VOID ProbeIsr(VOID)
{
    g_probeCount++;
}

/* read an AArch64 system register by encoding, e.g. PROBE_MRS(v, "S3_0_c4_c6_0") */
#define PROBE_MRS(out, reg) __asm__ volatile("mrs %0, " reg : "=r"(out))

STATIC VOID SgiDiagTask(VOID)
{
    UINT64 mpidr;
    UINTPTR sgiBase;
    UINT32 lastEn = 0, lastPend = 0, lastAct = 0, lastIpr = 0;
    UINT32 curEn, curPend, curAct, curIpr;
    UINT32 i, ret, ret2, ret3;
    UINT64 vPmr, vGrpEn1, vRpr, vHppir, vCtlr;
    extern UINT32 ArchHwiCreate(HWI_HANDLE_T hwiNum, HWI_PRIOR_T hwiPrio, HWI_MODE_T hwiMode,
                                HWI_PROC_FUNC hwiHandler, HwiIrqParam *irqParam);
    extern UINT32 ArchHwiDelete(HWI_HANDLE_T hwiNum, HwiIrqParam *irqParam);
    extern UINT32 LOS_HwiCreate(HWI_HANDLE_T hwiNum, HWI_PRIOR_T hwiPrio, HWI_MODE_T hwiMode,
                                HWI_PROC_FUNC hwiHandler, HwiIrqParam *irqParam);
    extern UINT32 LOS_HwiEnable(HWI_HANDLE_T hwiNum);
    extern UINT32 LOS_HwiTrigger(HWI_HANDLE_T hwiNum);
    extern UINT32 LOS_HwiDelete(HWI_HANDLE_T hwiNum, HwiIrqParam *irqParam);

    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    sgiBase = 0xFD460000UL + (UINTPTR)(((mpidr >> 8) & 0xFFU) * 0x20000UL) + 0x10000UL;

    /* phase 1: transition tracer, 400 x 20 ticks = 80 s */
    for (i = 0; i < 400; i++) {
        LOS_TaskDelay(20);
        curEn = *(volatile UINT32 *)(sgiBase + 0x0100U);
        curPend = *(volatile UINT32 *)(sgiBase + 0x0280U);
        curAct = *(volatile UINT32 *)(sgiBase + 0x0380U);
        curIpr = *(volatile UINT32 *)(sgiBase + 0x0404U);
        if ((curEn != lastEn) || (curPend != lastPend) ||
            (curAct != lastAct) || (curIpr != lastIpr)) {
            PRINTK("[sgitrace] t=%u en=%08x pend=%08x act=%08x ipr47=%08x\n",
                   (UINT32)LOS_TickCountGet(), curEn, curPend, curAct, curIpr);
            lastEn = curEn; lastPend = curPend; lastAct = curAct; lastIpr = curIpr;
        }
    }
    PRINTK("[sgitrace] done en=%08x pend=%08x act=%08x ipr47=%08x\n",
           lastEn, lastPend, lastAct, lastIpr);

    /* phase 2: probe v2 - suite path with readbacks */
    PROBE_MRS(vPmr, "S3_0_c4_c6_0");
    PROBE_MRS(vGrpEn1, "S3_0_c12_c12_7");
    PROBE_MRS(vRpr, "S3_0_c12_c11_3");
    PROBE_MRS(vHppir, "S3_0_c12_c12_2");
    PROBE_MRS(vCtlr, "S3_0_c12_c12_4");
    PRINTK("[sgiprobe] calc=%u rdctlr=%08x igrp=%08x mod=%08x\n",
           (UINT32)((mpidr >> 8) & 0xFFU),
           *(volatile UINT32 *)(sgiBase - 0x10000UL),
           *(volatile UINT32 *)(sgiBase + 0x0080U),
           *(volatile UINT32 *)(sgiBase + 0x0d00U));
    PRINTK("[sgiprobe] sys pmr=%02x igrpen1=%x rpr=%02x hppir1=%03x ctlr=%x\n",
           (UINT32)vPmr, (UINT32)vGrpEn1, (UINT32)vRpr, (UINT32)vHppir, (UINT32)vCtlr);

    /* A: exact suite sequence: create -> enable(readback) -> trigger -> keep enabled */
    g_probeCount = 0;
    ret = LOS_HwiCreate(6, 3, 0, ProbeIsr, NULL);
    PRINTK("[sgiprobe] A1 create ret=%x en=%08x pend=%08x\n", ret,
           *(volatile UINT32 *)(sgiBase + 0x0100U),
           *(volatile UINT32 *)(sgiBase + 0x0280U));
    ret = LOS_HwiEnable(6);
    curEn = *(volatile UINT32 *)(sgiBase + 0x0100U);
    PRINTK("[sgiprobe] A2 enable ret=%x en=%08x bit6=%u rdctlr=%08x\n", ret, curEn,
           (curEn >> 6) & 1U, *(volatile UINT32 *)(sgiBase - 0x10000UL));
    ret = LOS_HwiTrigger(6);
    LOS_TaskDelay(10); /* no delete before delivery lands */
    PROBE_MRS(vRpr, "S3_0_c12_c11_3");
    PROBE_MRS(vHppir, "S3_0_c12_c12_2");
    PRINTK("[sgiprobe] A3 trig ret=%x cnt=%u en=%08x pend=%08x act=%08x rpr=%02x hppir1=%03x\n",
           ret, g_probeCount,
           *(volatile UINT32 *)(sgiBase + 0x0100U),
           *(volatile UINT32 *)(sgiBase + 0x0280U),
           *(volatile UINT32 *)(sgiBase + 0x0380U), (UINT32)vRpr, (UINT32)vHppir);

    if (g_probeCount == 0) {
        /* B: stuck pend - ArchHwiCreate on top (legacy form still free after LOS path) */
        ret = ArchHwiCreate(6, 3, 0, ProbeIsr, NULL);
        LOS_TaskDelay(10);
        PRINTK("[sgiprobe] B1 archcreate ret=%x cnt=%u en=%08x pend=%08x act=%08x\n", ret,
               g_probeCount,
               *(volatile UINT32 *)(sgiBase + 0x0100U),
               *(volatile UINT32 *)(sgiBase + 0x0280U),
               *(volatile UINT32 *)(sgiBase + 0x0380U));
        ret = LOS_HwiTrigger(6);
        LOS_TaskDelay(10);
        PRINTK("[sgiprobe] B2 trig ret=%x cnt=%u pend=%08x\n", ret, g_probeCount,
               *(volatile UINT32 *)(sgiBase + 0x0280U));
        (VOID)ArchHwiDelete(6, NULL);
    } else {
        (VOID)LOS_HwiDelete(6, NULL);
    }

    /* C: fresh SGI5 full LOS cycle */
    g_probeCount = 0;
    ret = LOS_HwiCreate(5, 3, 0, ProbeIsr, NULL);
    ret2 = LOS_HwiEnable(5);
    ret3 = LOS_HwiTrigger(5);
    LOS_TaskDelay(10);
    PRINTK("[sgiprobe] C create=%x en=%x trig=%x cnt=%u en=%08x pend=%08x act=%08x\n",
           ret, ret2, ret3, g_probeCount,
           *(volatile UINT32 *)(sgiBase + 0x0100U),
           *(volatile UINT32 *)(sgiBase + 0x0280U),
           *(volatile UINT32 *)(sgiBase + 0x0380U));
    (VOID)LOS_HwiDelete(5, NULL);
    LOS_TaskDelay(2);
    PRINTK("[sgiprobe] done en=%08x pend=%08x act=%08x\n",
           *(volatile UINT32 *)(sgiBase + 0x0100U),
           *(volatile UINT32 *)(sgiBase + 0x0280U),
           *(volatile UINT32 *)(sgiBase + 0x0380U));
}
#endif /* LOSCFG_TEST SGI diag */

LITE_OS_SEC_TEXT_INIT INT32 main(VOID)
{
    UINT32 ret;

    /* 1. board: console (UART3) + caches */
    board_init();

    PRINTK("****************************************************\n");
    PRINTK(" RK3568 AMP liteos_m @ CPU3 (AArch64 EL1)\n");
    PRINTK("****************************************************\n");

    /* 2. kernel: memory/GICv3(ArchInit)/tick/task/ipc/swtmr/idle */
    ret = LOS_KernelInit();
    if (ret != LOS_OK) {
        PRINT_ERR("[boot] LOS_KernelInit failed: 0x%x\n", ret);
        while (1) { }
    }

#ifdef LOSCFG_MCS_NET
    /* Defensive cleanup of the GMAC1 SPI (INTID 64) before the scheduler
     * enables interrupts. A previous slave run torn down by the master
     * (e.g. the rpmsg demo powering the core off mid-test, before the
     * EtherCAT self-test could run NetStop) leaves the DMA channel
     * status latched RI/NIS with the line enabled in the GICD. The next
     * boot then takes the stray SPI the moment interrupts open - into a
     * vector nobody registered yet - and hangs right after
     * "Entering scheduler" (observed 2026-09-23). Kill it at the source
     * first (mask + W1C, so the level line actually drops), then clear
     * GICD pending and disable. NetStart's ArchHwiCreate (gic-v3
     * createIrq, per-core ownership) re-establishes the full contract -
     * group/route/trigger/priority/enable - when it runs. */
    {
        volatile UINT32 *dmaIena = (volatile UINT32 *)(0xFE010000UL + 0x1134UL);
        volatile UINT32 *dmaStat = (volatile UINT32 *)(0xFE010000UL + 0x1160UL);
        volatile UINT32 *icenabler = (volatile UINT32 *)(0xFD400000UL + 0x0188UL);
        volatile UINT32 *icpendr = (volatile UINT32 *)(0xFD400000UL + 0x0288UL);

        *dmaIena = 0;             /* mask everything at the source */
        *dmaStat = 0xFFFFFFFFUL;  /* W1C: drop the level line */
        *icenabler = 1u;          /* GICD: disable INTID 64 */
        *icpendr = 1u;            /* GICD: clear pending */
        __asm__ volatile ("dsb sy" ::: "memory");
    }
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
#if defined(LOSCFG_RK3568_UART) && defined(LOSCFG_SHELL)
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

    /* 4. mica/rpmsg bring-up runs in the mica task below, after the
     * scheduler started (the openamp_init.c manual path was removed:
     * libmica's pedestal owns metal_init, the notify IRQ and the
     * resource table). */

    /* 5. test suite (debug builds) */
#if defined(LOSCFG_TEST)
    /* MCS_TEST_STARTUP_DELAY: yield the CPU for a while so the mica task
     * (lower prio) can finish service creation and the NS bind handshakes
     * before the test suite starts to hog the CPU; otherwise the slave
     * epts miss the master bind announce and stay not-ready forever. */
    {
        TSK_INIT_PARAM_S diagParam = {0};
        UINT32 diagTaskId;

        diagParam.pfnTaskEntry = (TSK_ENTRY_FUNC)SgiDiagTask;
        diagParam.pcName = "SgiDiag";
        diagParam.usTaskPrio = 24; /* above mica(26) and the test suite(25) */
        diagParam.uwStackSize = 0x1800;
        if (LOS_TaskCreate(&diagTaskId, &diagParam) != LOS_OK) {
            PRINT_ERR("[boot] SgiDiag task create failed\n");
        }
    }
    LOS_TaskDelay(3000);
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

#if defined(LOSCFG_MCS_NET) && !defined(LOSCFG_TEST)
    /* Real-time NIC user demo (GMAC1 data plane; master mcs_net.ko must
     * have brought the port up first - see net_test.c). Skipped in test
     * builds: the interface suites (testsuites/sample/mcs) own the port,
     * a running demo would make every NetInit fail with -EBUSY. */
    {
        extern UINT32 NetTestTaskInit(VOID);
        ret = NetTestTaskInit();
        if (ret != LOS_OK) {
            PRINT_ERR("[boot] NetTestTaskInit failed: 0x%x\n", ret);
        }
    }
#endif

#if defined(LOSCFG_MCS_CAN) && !defined(LOSCFG_TEST)
    /* Real-time CAN user demo (classic CAN 2.0B data plane; master
     * mcs_can.ko must have handed CAN1 over first - see can_test.c).
     * Skipped in test builds for the same reason as the NIC demo. */
    {
        extern UINT32 CanTestTaskInit(VOID);
        ret = CanTestTaskInit();
        if (ret != LOS_OK) {
            PRINT_ERR("[boot] CanTestTaskInit failed: 0x%x\n", ret);
        }
    }
#endif

#if defined(LOSCFG_MCS_RPC) && !defined(LOSCFG_TEST)
    /* mica RPC service validation + BSD-socket proxy demo: the slave app
     * calls np_* APIs that travel over the rpmsg-rpc endpoint and execute
     * on the master Linux stack (see netproxy_test.c + netproxy_demo.c).
     * Skipped in test builds like the NIC/CAN demos: the np suite in
     * testsuites/sample/mcs owns the rpc channel, and a racing demo task
     * issuing np_* calls before the services are up breaks the rpc
     * endpoint bring-up (observed: ept never ready, all Rpc/Np cases
     * fail with -EAGAIN/-1). */
    {
        extern UINT32 NetProxyTestTaskInit(VOID);
        ret = NetProxyTestTaskInit();
        if (ret != LOS_OK) {
            PRINT_ERR("[boot] NetProxyTestTaskInit failed: 0x%x\n", ret);
        }
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
    }
#endif

    LOS_Start();

    while (1) { }
    return 0;
}
