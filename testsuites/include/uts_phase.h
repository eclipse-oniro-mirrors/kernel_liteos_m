/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
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
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _UTS_PHASE_H
#define _UTS_PHASE_H

#include "los_typedef.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/*
 * UTS (Unified Test Suite) two-phase handshake.
 *
 * The kernel coordinator (IT_TST_INI, TestTaskEntry in osTest.c) and the
 * usr runner (the boxA test entry, usr_demo/tboxA/boxA_test_main.c)
 * synchronize through g_utsPhase in COMMON_RAM (MPU R1: RW from both
 * worlds) - no new syscall ABI, no cross-pool SIDs:
 *
 *   kernel: OsBoxInit -> kernel suites -> g_utsPhase = GO
 *   usr:    park while != GO -> usr suites -> g_utsPhase = DONE
 *   kernel: poll (bounded) for DONE -> unified final report
 *
 * The variable is DEFINED in src/uts_phase.c, compiled once in the
 * usr_demo box_usr_test module so it lands in COMMON_RAM via the lds
 * file-name routing; non-BOX builds never link it.
 */

#define UTS_PHASE_IDLE 0u /* zeroed by startup: boxes not yet released */
#define UTS_PHASE_GO   1u /* kernel phase finished, usr phase may run */
#define UTS_PHASE_DONE 2u /* usr phase finished, final report may print */

/*
 * MULTI-BOX CONCURRENCY
 * ---------------------------------------------------------------------------
 * This build runs TWO usr suites (boxA and boxB) AT THE SAME TIME. That is
 * deliberate: the point is to prove the boxes are isolated, so they must run
 * concurrently and still not disturb each other.
 *
 * What makes concurrency safe is NOT this file - it is the per-world isolation
 * of the iCunit runner (see iCunit.inc: pass/fail totals, case slot, errLine,
 * module stats are all indexed by the calling world). Without that, two suites
 * sharing one case slot and one errLine would corrupt each other's results and
 * could even report a failure as a pass.
 *
 * The handshake here is correspondingly simple:
 *
 *     kernel:  g_utsPhase = GO          (broadcast: every box may start)
 *              wait until g_utsDoneCount == g_utsBoxCount
 *     box:     park while g_utsPhase != GO
 *              run its suite
 *              g_utsDoneCount++          (under the scheduler lock)
 *
 * g_utsBoxCount is published by the kernel (it can read the box config, the
 * boxes cannot) so the wait has an exact expected value instead of a timeout.
 */


/*
 * Coordinator wait loop: UTS_USR_TIMEOUT_POLLS x UTS_USR_POLL_TICKS ticks.
 *
 * NOTE this board's tick measures ~1s (not the configured 1ms - pre-existing
 * SysTick quirk, see the calibrate line), so the budget is much longer in wall
 * clock than the numbers suggest.
 *
 * History, so nobody has to rediscover it:
 *   120 x 5 = 600 ticks was TOO SHORT once a second usr suite was added. boxA
 *   needs ~650 ticks (its Tsk group parks on LOS_TaskDelay between probe
 *   create/exit/recycle rounds), so the coordinator timed out with FOUR boxA
 *   cases and ALL FIVE boxB cases still pending:
 *
 *       [UTS] usr phase HUNG (timeout) at token 1, kernel results only
 *       ... 164 ms later ...
 *       UTS: boxA done, handing over to boxB
 *
 *   The report showed 33 of 37 boxA cases and none of boxB's - not because
 *   anything was miscounted, but because the run was cut short.
 *
 * 600 x 2 = 1200 ticks gives ~2x headroom over the measured boxA cost, and the
 * finer 2-tick poll keeps the HUNG verdict prompt. A worst-case boxA (every
 * settle loop running to its full spin budget) would still exceed this - which
 * is correct: that is a genuinely stuck run and must be reported as HUNG rather
 * than silently truncated.
 */
#define UTS_USR_TIMEOUT_POLLS 600
#define UTS_USR_POLL_TICKS    2

extern volatile UINT32 g_utsPhase;

/* Number of usr suites released; set by the kernel before GO. */
extern volatile UINT32 g_utsBoxCount;

/* Sequential-mode baton: boxA raises it, boxB waits on it.
 * Unused when UTS_CONCURRENT_MODE == 1. */
extern volatile UINT32 g_utsTurnB;

/*
 * Completion flags, ONE SLOT PER WORLD - deliberately NOT a shared counter.
 *
 * A shared `g_utsDoneCount++` needs a lock, and a box cannot take one:
 * SCHEDULER_LOCK expands to LOS_SpinLockSave -> ArchIntLock (PRIMASK/cpsid),
 * which lives in KERNEL_FLASH. Executing it from user mode is an IACCVIOL -
 * measured: PC = ArchIntLock+0x0, LR = boxBTestEntry+0x65, box destroyed.
 * boxB also has no mux left to use instead (its single slot is the heap lock).
 *
 * So the design removes the sharing instead of guarding it: each box writes
 * only its own slot (single writer, no read-modify-write), and the kernel scans
 * them. On this single-core target a plain aligned store is atomic, so no lock
 * is required at all.
 *
 *     world 0 = kernel (never written here)
 *     world 1 = boxA
 *     world 2 = boxB
 */
/*
 * CONCURRENCY SWITCH - the A/B lever for the box-interference investigation.
 *
 *   1  both boxes are released by the SAME GO broadcast and run their suites
 *      at the same time. This is the configuration that exposes races, and the
 *      one under test.
 *   0  boxB parks until boxA has finished (sequential). Use it as the control:
 *      if the same failure disappears here, the defect is concurrency-specific
 *      rather than a plain logic bug.
 *
 * The flag only changes WHEN the second box is released; everything else (the
 * per-world iCunit state, the done flags, the suites themselves) is identical,
 * so a difference between the two runs is attributable to overlap alone.
 *
 * NOTE g_utsTurnB is the sequential-mode baton: boxA raises it on completion and
 * boxB waits on it. In concurrent mode it is never written.
 */
#ifndef UTS_CONCURRENT_MODE
#define UTS_CONCURRENT_MODE  1
#endif

#ifdef LOSCFG_TEST_BOXB_READ_BOXA_RAM
#define UTS_PROBE_ARMED 1U
#define UTS_PROBE_RETURNED 2U
extern volatile UINT32 g_utsProbeState;       /* boxB writes */
extern volatile UINT32 g_utsProbeDestroyed;   /* kernel releases boxA */
#endif

#define UTS_DONE_WORLD_NUM  3
extern volatile UINT32 g_utsDoneByWorld[UTS_DONE_WORLD_NUM];

/* Convenience for the coordinator: TRUE when every released suite is done. */
extern UINT32 ICunitUtsAllDone(UINT32 boxCount);

/*
 * "Is boxB taking part?" - decided by the KERNEL and published here.
 *
 * boxA's entry runs in USER mode and therefore CANNOT read g_userBoxCBArr:
 * that array lives in KERNEL_RAM (0x20000320 on this target) and an
 * unprivileged load from there is a MemManage DACCVIOL - measured, right after
 * boxA finished its suites (PC = boxTestEntry+0x44, FaultAddr = &g_userBoxCBArr
 * [1].boxEntryTask = 0x20000480). So the box does not ask "does boxB exist?";
 * the coordinator that CAN see the config answers once, here, in COMMON_RAM
 * (MPU R1, readable by both worlds).
 *
 *   1  boxA hands the baton to boxB after its own run
 *   0  boxA ends the chain directly (boxB has no entry task this build)
 */
extern volatile UINT32 g_utsHasBoxB;

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _UTS_PHASE_H */
