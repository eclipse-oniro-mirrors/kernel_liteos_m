/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/* ============================================================================
 * boxB_test_main.c - boxB UTS usr-phase entry (test builds only).
 *
 * Under LOSCFG_KERNEL_TEST boxes_auto.cfg points boxB's entry at boxBTestEntry;
 * in demo builds it stays boxBEntry (the "Hello boxB!" loop).
 *
 * ---------------------------------------------------------------------------
 * WHY boxB EXISTS IN THE TEST TREE
 * ---------------------------------------------------------------------------
 * boxA's heap is shared by ~30 cases, so destroying or exhausting it there
 * would poison every later measurement. boxB has no other consumer, which makes
 * the destructive heap lifecycle (LOS_MemDeInit / LOS_MemInit) testable - see
 * testsuites/sample/usr/boxB/It_los_boxB_usr_003.c.
 *
 *   FLASH  64K at 0x08030000   (BOX_B_FLASH, same size as boxA's slot)
 *   RAM    16K at 0x2001C000   (BOX_B_RAM)
 *            .bss ~4.1K  (g_boxbStack 4096 + a few counters)
 *            heap ~12.2K (BIGGER than boxA's ~7K - the reason it is the better
 *                        place to drain an allocator)
 *
 * File name carries "boxB" on purpose: the boxes.ld selectors route every
 * *boxB*.o object into the boxB flash/RAM slots (user-executable).
 * ========================================================================== */

#include "boxes_conf.h"
/* los_task.h is required, not optional: boxes_res_conf.h (pulled by
 * boxes_auto.cfg) declares g_boxaEvtCB / g_boxbMuxCB etc. by TYPE, and the
 * LosEventCB / LosMuxCB / LosSemCB / LosQueueCB typedefs arrive through this
 * chain. Omitting it fails with "unknown type name 'LosEventCB'" at the
 * externs, not at any use site. boxA_test_main.c includes it for the same
 * reason. */
#include "los_task.h"
#include "los_memory.h"
#include "los_debug.h"
#include "syscalls.h"
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
#include "los_swtmr_usr_pri.h"
#endif

#include "uts_phase.h"

extern VOID ItSuiteLosBoxBUsr(VOID);

VOID boxBTestEntry(VOID)
{
    /* Build this box's heap from USER context.
     *
     * The pool lock has to be created HERE, not kernel-side: under BOX the
     * allocator is compiled once in the usr world, so MemLockCreate's
     * LOS_MuxCreate is the usr wrapper -> OsMuxCreate, which draws the CB from
     * boxB's own mux pool and the sId from boxB's SID pool. A lock created
     * kernel-side would carry a kernel-head sId over a kernel-table CB that
     * boxB's later LOS_MuxPend would have to resolve cross-domain.
     *
     * NOTE boxB has LOSCFG_BOXB_MUX_LIMIT = 1 and LOSCFG_BOXB_SID_LIMIT = 1, so
     * this single call consumes both pools completely. Nothing else in this box
     * may create an IPC object - which is exactly why the boxB suite is
     * heap-only. */
    UINT32 ret = LOS_MemInit((VOID *)OS_BOXB_POOL_ADDR, (UINT32)BOXB_HEAP_SIZE);
    if (ret != LOS_OK) {
        LOS_PrintStr("UTS: boxB heap LOS_MemInit failed!\n");
        /* Do NOT return: the coordinator waits on the run token, and a task
         * that exits here would leave the whole usr phase to time out. Hand the
         * baton straight back and park. */
        g_utsDoneByWorld[2] = 1U;   /* ours alone; no lock (see below) */
        while (1) {
            LOS_TaskDelay(0xFFFF);
        }
    }

    /* Wait for our turn. boxA runs first and passes the baton here, so the two
     * boxes never interleave inside the shared iCunit counters. Same unbounded
     * poll as boxA, same rationale: if the kernel phase reports HUNG we stay
     * parked out of the way instead of corrupting its results. */
#if (UTS_CONCURRENT_MODE == 1) || defined(LOSCFG_TEST_BOXB_READ_BOXA_RAM)
    /* Same broadcast as boxA: the kernel sets GO once and every box starts at
     * the same time. Concurrency is the point of this build. */
    while (g_utsPhase != UTS_PHASE_GO) {
        LOS_TaskDelay(1);
    }
#else
    /* Sequential control: wait not only for GO but for boxA to finish, so the
     * two suites never overlap. Everything else is identical to the concurrent
     * run, which is what makes the comparison meaningful. */
    while ((g_utsPhase != UTS_PHASE_GO) || (g_utsTurnB == 0U)) {
        LOS_TaskDelay(1);
    }
#endif

#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
    /* Bring up this box's timer task.
     *
     * This takes the SECOND of boxB's two TCBs (the entry task holds the first)
     * and needs a swtmr CB from the box pool. It is done here, before the test
     * phase, so that the heap measured by the cases already includes the timer
     * task's static stack - the numbers stay stable across the suite.
     *
     * Do NOT remove this to "free a slot": with TASK_LIMIT = 2 the suite cannot
     * spawn tasks anyway, and the timer task is what proves the box's pool
     * wiring works at all. */
    ret = BoxSwtmrInit(LOS_BOXB_ID, 1024);
    if (ret != LOS_OK) {
        PRINTK("UTS: boxB BoxSwtmrInit failed 0x%x\n", ret);
    }
#endif

    /* Banner deliberately loud: this suite is the second usr phase and its
     * cases contribute to the SAME iCunit counters as boxA, so a missing banner
     * unambiguously means boxB never ran (rather than "ran but was not counted").
     * ICunitAddCase is called per case and each call runs immediately, so every
     * boxB case is exactly one +1 in g_passResult/g_failResult. */
    PRINTK("\n================ UTS usr phase (boxB, user mode) ================\n");
    dprintf("[BOXB] entering boxB suite: heap=0x%x size=%u\n",
            (UINT32)OS_BOXB_POOL_ADDR, (UINT32)BOXB_HEAP_SIZE);
    ItSuiteLosBoxBUsr();
#ifdef LOSCFG_TEST_BOXB_READ_BOXA_RAM
    /* This diagnostic must fault on the load. Never expose the loaded value. */
    dprintf("[BOXB] MPU probe: reading boxA private RAM at 0x%x\n", (UINT32)OS_BOXA_POOL_ADDR);
    g_utsProbeState = UTS_PROBE_ARMED;
    (void)*(volatile const UINT32 *)OS_BOXA_POOL_ADDR;
    g_utsProbeState = UTS_PROBE_RETURNED;
    dprintf("[BOXB] FAIL: boxA private RAM read returned\n");
    for (;;) {
        LOS_TaskDelay(0xFFFF);
    }
#endif
    dprintf("[BOXB] boxB suite finished\n");
    PRINTK("================ UTS usr phase (boxB) done ================\n");

    /* End of the chain: the coordinator may print the unified report now.
     * Everything boxA and boxB accumulated has already landed in the shared
     * iCunit counters by this point. */
    /* Same as boxA: own slot, no lock. SCHEDULER_LOCK is NOT usable from a box
     * (ArchIntLock lives in kernel flash), and boxB has no mux left to use
     * instead. One writer + one aligned store = atomic here. */
    g_utsDoneByWorld[2] = 1U;
    PRINTK("UTS: boxB suite done (%u of %u)\n", 1U, g_utsBoxCount);

    /* Park: the entry task remains as the box anchor. Returning would delete
     * the box's only guaranteed TCB, and boxB has no spare to rebuild it. */
    while (1) {
        LOS_TaskDelay(0xFFFF);
    }
}
