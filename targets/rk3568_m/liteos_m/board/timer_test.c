/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * timer_test.c - user demo for the slave-side shared-free timer
 * (TIMER1, one timerDevice per port; TIMER0 belongs to the master
 * clockevent, see timer0_profile.h).
 * compiled in via LOSCFG_MCS_TIMER (board/BUILD.gn), launched from
 * main.c. every phase prints one line per check point and a Stop line;
 * a phase passes when all counts match and the tick span matches the
 * programmed duration (the minimal contract exposes no elapsed
 * readout, so timing checks are tick-level). "[timer] all phases
 * done" = channel healthy.
 * phases (each: init -> start -> wait -> verify -> stop/deinit):
 *   1. periodic-irq-10ms  20 expiries in 200ms (tick-level check)
 *   2. oneshot-irq-50ms   exactly one callback, channel silent after
 *   3. rearm-5ms          SetInterval on the fly, 10 expiries at 5ms
 * replace the phase bodies with your own periodic scheduling task.
 */

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_task.h"
#include "target_config.h"
#include "timer.h"
#include "timer0_profile.h"
#include <mica/rpc.h>
#include <mica/rpc_proto.h>
#ifdef LOSCFG_SHELL
#include "shell.h"
#endif

/* all tick math must go through the real tick rate (rk3568_m runs at
 * 1000Hz - 1ms/tick); the original 100Hz assumptions shrank every
 * deadline window 10x and broke all phases at a healthy timer. */
#define TIMER_DEMO_US_PER_TICK    (1000000ULL / LOSCFG_BASE_CORE_TICK_PER_SECOND)
#define TIMER_DEMO_TIMEOUT_TICKS  \
    ((UINT32)(200ULL * LOSCFG_BASE_CORE_TICK_PER_SECOND / 1000ULL))
/* tick-level span tolerance: tick resolution + isr latency slack */
#define TIMER_DEMO_SPAN_SLACK_TICKS  2u

/* log forwarder: demo output goes to the master console via RPC_FUNC_LOG
 * (fire-and-forget). the shared slave UART is unreliable for the demo:
 * the net demo floods PRINTK at 200Hz and starves lower-priority PRINTK
 * callers (observed: full demo output wedged for minutes). */
static void TimerDemoLog(const char *fmt, ...)
{
    char text[160];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (len <= 0) {
        return;
    }
    (VOID)mica_rpc_call(RPC_FUNC_LOG, text, (UINT32)len + 1,
                        NULL, 0, NULL, 3000);
}

/* ------------------------------------------------------------------ */
/* shared: irq-driven expiry recorder                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    timerDevice *dev;
    volatile uint32_t count;
} timerDemoCtx;

static timerDemoCtx g_ctx;

static void TimerDemoCb(void *arg)
{
    timerDemoCtx *c = (timerDemoCtx *)arg;

    c->count++;
}

/* verify the tick span between two tick snapshots matches wantUs
 * (tick-level check); 0 = ok, 1 = drift. */
static int TimerDemoCheckSpan(UINT64 startTick, UINT64 endTick,
                              uint64_t wantUs, const char *name)
{
    UINT64 span = endTick - startTick;
    UINT64 expect = wantUs / TIMER_DEMO_US_PER_TICK;
    UINT64 diff = (span > expect) ? span - expect : expect - span;

    if (diff > TIMER_DEMO_SPAN_SLACK_TICKS) {
        TimerDemoLog("[timer] [%s] span drift: %llu ticks (want %llu)\n",
               name, (unsigned long long)span, (unsigned long long)expect);
        return 1;
    }
    return 0;
}

static int TimerDemoRunIrqPhase(const char *name, uint32_t mode,
                                uint64_t intervalUs, uint32_t wantCount,
                                uint32_t settleTicks)
{
    timerConfig cfg = {
        .portId = TIMER_PORT_TIMER1_RK3568,
        .mode = mode,
        .intervalUs = intervalUs,
        .callback = TimerDemoCb,
        .callbackArg = &g_ctx,
    };
    timerDevice *dev = NULL;
    UINT32 deadline = LOS_TickCountGet() + TIMER_DEMO_TIMEOUT_TICKS +
                      (UINT32)(intervalUs * (uint64_t)(wantCount + 4) /
                               TIMER_DEMO_US_PER_TICK + 1);
    UINT64 startTick, endTick;
    int fail = 0;

    (VOID)memset(&g_ctx, 0, sizeof(g_ctx));
    if (TimerInit(&cfg, &dev) != 0) {
        TimerDemoLog("[timer] [%s] init FAILED\n", name);
        return 1;
    }
    g_ctx.dev = dev;
    TimerDemoLog("[timer] [%s] up\n", name);
    startTick = (UINT64)LOS_TickCountGet();
    if (TimerStart(dev) != 0) {
        TimerDemoLog("[timer] [%s] start FAILED\n", name);
        (VOID)TimerDeinit(dev);
        return 1;
    }

    while ((g_ctx.count < wantCount) &&
           (LOS_TickCountGet() < deadline)) {
        (VOID)LOS_TaskDelay(1);
    }
    endTick = (UINT64)LOS_TickCountGet();
    /* let a would-be extra expiry land (oneshot overrun check) */
    (VOID)LOS_TaskDelay(settleTicks);
    (VOID)TimerStop(dev);

    if (g_ctx.count != wantCount) {
        TimerDemoLog("[timer] [%s] count mismatch: got %u want %u\n",
               name, g_ctx.count, wantCount);
        fail = 1;
    }
    if (mode == TIMER_MODE_PERIODIC) {
        /* whole-run duration: wantCount * intervalUs, tick-level */
        fail += TimerDemoCheckSpan(startTick, endTick,
                                   intervalUs * (uint64_t)(wantCount - 1), name);
    }
    TimerDemoLog("[timer] [%s] Stop: %d (fired %u)\n", name, fail,
           g_ctx.count);
    (VOID)TimerDeinit(dev);
    return fail;
}

/* ------------------------------------------------------------------ */
/* rearm on the fly                                                     */
/* ------------------------------------------------------------------ */

static int TimerDemoRearmPhase(void)
{
    timerConfig cfg = {
        .portId = TIMER_PORT_TIMER1_RK3568,
        .mode = TIMER_MODE_PERIODIC,
        .intervalUs = 20000,    /* start at 20ms */
        .callback = TimerDemoCb,
        .callbackArg = &g_ctx,
    };
    timerDevice *dev = NULL;
    UINT32 deadline = LOS_TickCountGet() + TIMER_DEMO_TIMEOUT_TICKS +
                      (UINT32)(80000ULL / TIMER_DEMO_US_PER_TICK + 1); /* 20ms old + 10x5ms new */
    UINT64 startTick, endTick;
    int fail = 0;

    (VOID)memset(&g_ctx, 0, sizeof(g_ctx));
    if (TimerInit(&cfg, &dev) != 0) {
        TimerDemoLog("[timer] [rearm-5ms] init FAILED\n");
        return 1;
    }
    g_ctx.dev = dev;
    TimerDemoLog("[timer] [rearm-5ms] up\n");
    if (TimerStart(dev) != 0) {
        TimerDemoLog("[timer] [rearm-5ms] start FAILED\n");
        (VOID)TimerDeinit(dev);
        return 1;
    }
    /* first interval 20ms: wait one expiry, then shrink to 5ms */
    while ((g_ctx.count < 1) && (LOS_TickCountGet() < deadline)) {
        (VOID)LOS_TaskDelay(1);
    }
    if (TimerSetInterval(dev, 5000) != 0) {
        TimerDemoLog("[timer] [rearm-5ms] SetInterval FAILED\n");
        (VOID)TimerStop(dev);
        (VOID)TimerDeinit(dev);
        return 1;
    }
    /* 10 more expiries at the new 5ms period (50ms total, tick-level) */
    {
        uint32_t base = g_ctx.count;

        startTick = (UINT64)LOS_TickCountGet();
        while ((g_ctx.count < base + 10) &&
               (LOS_TickCountGet() < deadline)) {
            (VOID)LOS_TaskDelay(1);
        }
        endTick = (UINT64)LOS_TickCountGet();
        (VOID)TimerStop(dev);
        fail += TimerDemoCheckSpan(startTick, endTick, 50000ULL, "rearm-5ms");
    }
    TimerDemoLog("[timer] [rearm-5ms] Stop: %d\n", fail);
    (VOID)TimerDeinit(dev);
    return fail;
}

/* ------------------------------------------------------------------ */
/* entry                                                                */
/* ------------------------------------------------------------------ */

VOID TimerTestTaskEntry(VOID)
{
    int fail = 0;

    TimerDemoLog("[timer] demo start\n");
    fail += TimerDemoRunIrqPhase("periodic-irq-10ms", TIMER_MODE_PERIODIC,
                                 10000, 20, 2);
    fail += TimerDemoRunIrqPhase("oneshot-irq-50ms", TIMER_MODE_ONESHOT,
                                 50000, 1, 4);
    fail += TimerDemoRearmPhase();
    TimerDemoLog("[timer] all phases done%s\n", fail ? " (WITH FAILURES)" : "");
}

UINT32 TimerTestTaskInit(VOID)
{
    TSK_INIT_PARAM_S param = {0};
    UINT32 taskId;

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)TimerTestTaskEntry;
    param.usTaskPrio = 21;
    param.uwStackSize = 0x4000; /* PRINTK chain + phase frames are deep;
                                  * 0x1800 overflowed and corrupted heap */
    param.pcName = "timer_test";
    return LOS_TaskCreate(&taskId, &param);
}

#ifdef LOSCFG_SHELL
/* "timer" shell command: rerun the demo phases in a fresh task */
STATIC VOID TimerCmdRun(UINT32 argc, char **argv)
{
    (VOID)argc;
    (VOID)argv;
    if (TimerTestTaskInit() != LOS_OK) {
        TimerDemoLog("[timer] rerun task create failed\n");
    }
}

UINT32 TimerShellCmdRegister(VOID)
{
    return osCmdReg(CMD_TYPE_EX, "timer", XARGS, (CmdCallBackFunc)TimerCmdRun);
}
#endif /* LOSCFG_SHELL */
