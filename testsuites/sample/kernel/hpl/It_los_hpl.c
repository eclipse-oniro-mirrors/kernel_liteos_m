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

#include "osTest.h"
#include "It_los_hpl.h"

volatile UINT32 g_hplHookCalls = 0;
volatile UINT32 g_hplHookBytes = 0;
volatile UINT32 g_hplArgsEntryCnt = 0;
volatile UINT32 g_hplFmtEntryCnt = 0;
volatile UINT32 g_hplAbandonedCnt = 0;
volatile UINT32 g_hplCheckpointCnt = 0;

/* ARGS_RECORD_FUNC, runs in the compression task context. Scans the byte
 * stream for the little-endian protocol magics to maintain the statistics
 * asserted by the record cases. */
UINT32 TestHplRecordHook(const UINT8 *buf, UINT32 size)
{
    UINT32 i;
    UINT32 magic;
    UINT32 version;

    if (buf == NULL) {
        return LOS_OK;
    }
    g_hplHookCalls++;
    g_hplHookBytes += size;
    if (size < sizeof(UINT32)) {
        return LOS_OK;
    }
    for (i = 0; (i + sizeof(UINT32)) <= size; i++) {
        magic = (UINT32)buf[i] | ((UINT32)buf[i + 1] << 8) |
                ((UINT32)buf[i + 2] << 16) | ((UINT32)buf[i + 3] << 24);
        if (magic == HPL_ARGS_ENTRY_MAGIC) {
            g_hplArgsEntryCnt++;
        } else if (magic == HPL_FMT_ENTRY_MAGIC) {
            g_hplFmtEntryCnt++;
        } else if (magic == HPL_ABANDONED_LOG_ENTRY_MAGIC) {
            g_hplAbandonedCnt++;
        } else if (magic == HPL_CHECKPOINT_MAGIC) {
            /* The version field follows the magic. */
            if ((i + (sizeof(UINT32) * 2)) <= size) {
                version = (UINT32)buf[i + 4] | ((UINT32)buf[i + 5] << 8) |
                          ((UINT32)buf[i + 6] << 16) | ((UINT32)buf[i + 7] << 24);
                if (version == HPL_CHECKPOINT_VERSION) {
                    g_hplCheckpointCnt++;
                }
            }
        }
    }
    return LOS_OK;
}

/* Install the statistics hook and open all log levels. */
VOID TestHplPrepare(VOID)
{
    (VOID)LOS_HplSetLogRecordHook(TestHplRecordHook);
    (VOID)LOS_HplSetLogLevel(HPL_TEST_LOG_LEVEL);
}

VOID ItSuiteLosHpl(void)
{
    ItLosHpl001();
    ItLosHpl002();
    ItLosHpl003();
    ItLosHpl004();
    ItLosHpl005();
    ItLosHpl011();
    ItLosHpl012();
    ItLosHpl013();
    ItLosHpl014();
    ItLosHpl021();
    ItLosHpl022();
    ItLosHpl023();
    ItLosHpl024();
    ItLosHpl031();
}
