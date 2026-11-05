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
extern volatile UINT32 g_hplAbandonedCnt;  /* HplAbandonedLogEntry count */
extern volatile UINT32 g_hplCheckpointCnt; /* HplCheckPoint (version 1) count */

UINT32 TestHplRecordHook(const UINT8 *buf, UINT32 size);

/* TestHplPrepare: install the statistics record hook + open all log levels.
 *
 * NOTE: calling it first is a TEST-SIDE MITIGATION for a registered product
 * issue (hpl_test_design/问题记录/20261102_165607), NOT a product contract --
 * the functional design and los_hpl.h nowhere require a record hook to be
 * registered before using HPL. Root cause: with recordHook NULL,
 * OsHplDoCompressLog bails out at the checkpoint write (goto ERR,
 * los_hpl_compression.c:273-275) and skips the whole cycle body, including
 * the handle scan that frees preDelete'd staging buffers (a free that has
 * no dependency on the output sink). Consequences without the hook:
 *   - staging buffers of deleted tasks leak (ItLosHpl004 standalone run:
 *     asserts pass, +1040B ResLeak report; masked in full-suite runs where
 *     earlier cases installed the hook);
 *   - the format ring buffer is never drained, so new format registrations
 *     eventually fail and recording silently stops.
 * Until the product side decouples the reclaim scan from the sink, a case
 * MUST call it (or install any valid private hook, It_los_hpl_031.c style)
 * when it: 1) asserts on the g_hpl* statistics; 2) logs entries whose
 * delivery is counted; 3) creates a staging buffer freed via preDelete.
 * Pure level/readback/error-code probes (001/002) do not need it. Once the
 * product fix lands, requirement 3) disappears (keep it for 1)/2) only). */
VOID TestHplPrepare(VOID);

/* Raw compressed-stream capture used by the byte-layout verification cases.
 * TestHplProbeHook keeps every TestHplRecordHook statistic up to date and
 * additionally appends the stream bytes to g_hplProbeBuf; the case then walks
 * the captured entries (args/format/checkpoint/abandoned/custom) and asserts
 * per-format payload layout. Reset with g_hplProbeBytes = 0 before use. */
#define HPL_PROBE_BUF_SIZE 16384
extern UINT8 g_hplProbeBuf[HPL_PROBE_BUF_SIZE];
extern volatile UINT32 g_hplProbeBytes;
UINT32 TestHplProbeHook(const UINT8 *buf, UINT32 size);

VOID ItLosHpl001(VOID);
VOID ItLosHpl002(VOID);
VOID ItLosHpl003(VOID);
VOID ItLosHpl004(VOID);
VOID ItLosHpl005(VOID);
VOID ItLosHpl011(VOID);
VOID ItLosHpl012(VOID);
VOID ItLosHpl013(VOID);
VOID ItLosHpl014(VOID);
VOID ItLosHpl015(VOID);
VOID ItLosHpl021(VOID);
VOID ItLosHpl022(VOID);
VOID ItLosHpl023(VOID);
VOID ItLosHpl024(VOID);
VOID ItLosHpl031(VOID);
VOID ItLosHpl041(VOID);
VOID ItLosHpl042(VOID);
VOID ItLosHpl043(VOID);
VOID ItLosHpl044(VOID);
VOID ItLosHpl045(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _IT_LOS_HPL_H */
