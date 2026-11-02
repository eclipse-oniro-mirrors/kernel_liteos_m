/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
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
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifndef _IT_LOS_HPL_H
#define _IT_LOS_HPL_H

#include "osTest.h"
#include "los_hpl.h"
#include "los_hpl_pri.h"
#include "los_hpl_compression_pri.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* Open every log level so nothing is filtered during the record tests. */
#define HPL_TEST_LOG_LEVEL          (HPL_ERROR | HPL_WARNING | HPL_NOTICE | HPL_DEBUG)

/* Entries per burst: 50 entries overflow the 1024B default staging buffer and
 * wake the compression task immediately (HPL_TRIGGER_EVENT), so a short drain
 * delay is enough for the record hook to see the batch. */
#define HPL_TEST_LOG_COUNT          50

/* Polling guard (ticks) while waiting for worker-done flags. */
#define HPL_TEST_WAIT_LIMIT         100

/* Drain time after workers finish. The compress task waits on its event
 * with a LOS_MS2Tick(HPL_TASK_SLEEP_MS) = 1000ms timeout, so a burst tail
 * that did not fill the staging buffer and the pending preDelete frees are
 * both processed on that cycle: wait past it (1.2s) before asserting. */
#define HPL_TEST_DRAIN_TICKS        120

/* Interrupt-buffer logs are compressed on the compress-task sleep timeout. */
#define HPL_TEST_INT_DRAIN_TICKS    120

/* Staging buffer size used by the TaskHplSizeSet cases. */
#define HPL_TEST_STAGING_SIZE       1024

/* Statistics maintained by TestHplRecordHook (compress task context). */
extern volatile UINT32 g_hplHookCalls;
extern volatile UINT32 g_hplHookBytes;
extern volatile UINT32 g_hplArgsEntryCnt;   /* HplCompressedArgsEntry count */
extern volatile UINT32 g_hplFmtEntryCnt;    /* HplCompressedFormatEntry count */
extern volatile UINT32 g_hplAbandonedCnt;   /* HplAbandonedLogEntry count */
extern volatile UINT32 g_hplCheckpointCnt;  /* HplCheckPoint (version 1) count */

UINT32 TestHplRecordHook(const UINT8 *buf, UINT32 size);
VOID TestHplPrepare(VOID);

VOID ItLosHpl001(VOID);
VOID ItLosHpl002(VOID);
VOID ItLosHpl003(VOID);
VOID ItLosHpl004(VOID);
VOID ItLosHpl005(VOID);
VOID ItLosHpl011(VOID);
VOID ItLosHpl012(VOID);
VOID ItLosHpl013(VOID);
VOID ItLosHpl014(VOID);
VOID ItLosHpl021(VOID);
VOID ItLosHpl022(VOID);
VOID ItLosHpl023(VOID);
VOID ItLosHpl024(VOID);
VOID ItLosHpl031(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _IT_LOS_HPL_H */
