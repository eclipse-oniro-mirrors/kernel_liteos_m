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

/* Coordinator wait loop: 120 x 5 ticks. NOTE this board's tick measures
 * ~1s (not the configured 1ms - pre-existing SysTick quirk, see the
 * calibrate line), so this is a ~10min worst case; a hung usr case
 * reports HUNG and the run completes with the kernel results only. */
#define UTS_USR_TIMEOUT_POLLS 120
#define UTS_USR_POLL_TICKS    5

extern volatile UINT32 g_utsPhase;

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _UTS_PHASE_H */
