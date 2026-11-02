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

#include "los_hpl_logger_pri.h"
#include "los_hpl_ringbuf_pri.h"
#include "los_hpl_compression_pri.h"

#ifndef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
#include "los_hpl_c_preprocessor_pri.h"
#endif

#ifdef LOSCFG_DEBUG_HPL_EMULATION
#include "los_hpl_emulation_pri.h"
#endif

typedef struct {
    UINT32              timestampBytes;
    UINT32              totalSize;
    HplStageBuffer      *stageBuf;
    UncompressedEntry   *entry;
} HplArgumentsInfo;

UINT32 OsHplCalcBytes(UINT64 val)
{
    /* Total 64 bit, 7: up align, 8: calculate the max bytes to store value. */
    return ((64 - CLZLL(val) + 7) / 8);
}

STATIC VOID OsHplAbandonedlogRecord(HplRuntimeLogger *logger, UINT8 *writePos, UINT32 size)
{
    HplAbandonedLogEntry *entry = (HplAbandonedLogEntry *)writePos;
    UINT32 rdSize, peekSize = 0;
    UINT32 ret;
    UINT32 alignSize = (UINT32)sizeof(HplAbandonedLog);

    while (OsHplRingbufPeekUsedSize(&logger->abandonedLog, peekSize) != 0) {
        rdSize = OsHplRingbufPeekRead(&logger->abandonedLog, (CHAR *)(writePos + (UINT32)sizeof(HplAbandonedLogEntry)),
                                      (size - (UINT32)sizeof(HplAbandonedLogEntry)) / alignSize * alignSize, peekSize);
        if (rdSize == 0) {
            HPL_DEBUG_PRINT("Abandoned ringbuf read err\n");
            return;
        }
        peekSize = rdSize;
        entry->abandonEntryMagic = HPL_ABANDONED_LOG_ENTRY_MAGIC;
        entry->abandonNum = (rdSize / (UINT32)(sizeof(HplAbandonedLog)));
        ret = OsHplMetadataRecord(logger, writePos, (UINT32)sizeof(HplAbandonedLogEntry) + rdSize);
        if (ret != LOS_OK) {
            HPL_DEBUG_PRINT("abandon log metadata record failed\n");
            return;
        }
        OsHplRingbufReadPeekDone(&logger->abandonedLog, peekSize);
        peekSize = 0;
    }
}

STATIC INLINE HplArgsFragment* OsHplCreateArgsFragment(UINT8 *writePos)
{
    HplArgsFragment *argsFragment = NULL;

    /* Arguments header */
    argsFragment = (HplArgsFragment *)writePos;
    argsFragment->argsFragmentMagic = HPL_ARGS_FRAGMENT_MAGIC;
    argsFragment->fragmentType = ENTRY_TYPE_ARGS_FRAGMENT;
    argsFragment->argsDataBytes = (UINT32)sizeof(HplArgsFragment);
    argsFragment->totalArgsEntrys = 0;
    return argsFragment;
}

STATIC UINT32 OsHplCalcTimestampBytes(UINT64 fullyTimestamp, UINT64 logTimestamp)
{
    return OsHplCalcBytes(logTimestamp - fullyTimestamp);
}

STATIC VOID OsHplCreateArgEntry(UINT8 *writePos, UINT32 formatId, UINT32 timestampBytes, UINT32 size)
{
    HplCompressedArgsEntry *entry = (HplCompressedArgsEntry *)writePos;
    entry->argsEntryMagic = HPL_ARGS_ENTRY_MAGIC;
    entry->entryType = ENTRY_TYPE_ARGS_ENTRY;
    entry->additionalTimeStampBytes = timestampBytes;
    entry->totalEntryLength = size;
    entry->formatId = formatId;
    entry->protocolVersion = HPL_CHECKPOINT_VERSION;
}

STATIC UINT32 OsHplCompressWriteArgs(UINT8 **writePos, UINT32 *bufSize, HplArgumentsInfo *argsInfo,
                                     UINT64 fullyTimestamp, UINT32 *peekSize)
{
    UINT8 *wrAddr = *writePos;
    UINT32 freeSize = *bufSize;
    UINT32 needSize = argsInfo->totalSize;
    UINT32 timestampBytes = argsInfo->timestampBytes;
    HplStageBuffer *stageBuf = argsInfo->stageBuf;
    UncompressedEntry *ue = argsInfo->entry;
    UINT64 diffValue;
    UINT32 rdSize;
    errno_t err;

    OsHplCreateArgEntry(wrAddr, ue->fmtId, timestampBytes, needSize);
    wrAddr += (UINT32)sizeof(HplCompressedArgsEntry);
    freeSize -= (UINT32)sizeof(HplCompressedArgsEntry);

    diffValue = ue->timestamp - fullyTimestamp;
    err = memcpy_s(wrAddr, freeSize, &diffValue, timestampBytes);
    if (err != EOK) {
        return LOS_NOK;
    }
    wrAddr += timestampBytes;
    freeSize -= timestampBytes;

    /* Argument metadata. */
    rdSize = OsHplRingbufPeekRead(&stageBuf->buffer, (CHAR *)wrAddr,
                                  ue->entrySize - (UINT32)sizeof(UncompressedEntry), *peekSize);
    if ((rdSize != ue->entrySize - (UINT32)sizeof(UncompressedEntry))) {
        HPL_DEBUG_PRINT("Get arguments failed, rdSize %u, usedSize %u, needSize %u\n", rdSize,
                        OsHplRingbufPeekUsedSize(&stageBuf->buffer, *peekSize),
                        ue->entrySize - (UINT32)sizeof(UncompressedEntry));
        return LOS_NOK;
    }
    *peekSize += rdSize;
    wrAddr = wrAddr + ue->entrySize - (UINT32)sizeof(UncompressedEntry);
    freeSize -= (ue->entrySize - (UINT32)sizeof(UncompressedEntry));

    *writePos = wrAddr;
    *bufSize = freeSize;
    return LOS_OK;
}

STATIC UINT32 OsHplCompressTaskArgs(HplRuntimeLogger *logger, UINT8 *writePos, UINT32 size, HplHandle *hpl)
{
    HplArgsFragment             *argsFragment = OsHplCreateArgsFragment(writePos);
    HplStageBuffer              *stageBuf = &hpl->stageBuf;
    UINT8                       *wrAddr = writePos + (UINT32)sizeof(HplArgsFragment);
    UINT32                      freeSize = size - (UINT32)sizeof(HplArgsFragment);
    UINT64                      fullyTimestamp = 0;
    UINT32                      rdSize, peekSize = 0;
    HplArgumentsInfo            argsInfo;
    UncompressedEntry           ue;

    while (OsHplRingbufPeekUsedSize(&stageBuf->buffer, peekSize) != 0) {
        rdSize = OsHplRingbufPeekRead(&stageBuf->buffer, (CHAR *)&ue,
                                      (UINT32)sizeof(UncompressedEntry), peekSize);
        if ((rdSize != (UINT32)sizeof(UncompressedEntry)) || (ue.entryMagic != HPL_UNCOMPRESSED_ENTRY_MAGIC)) {
            HPL_DEBUG_PRINT("Uncompressed entry read err, usedSize %u\n",
                            OsHplRingbufPeekUsedSize(&stageBuf->buffer, peekSize));
            peekSize++;
            continue;
        }
        peekSize += rdSize;

#ifndef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
        /* Format info must be recorded firstly. */
        if (HPL_GET_FMT_ID(ue.fmtId) >= logger->fmtRecordCnt) {
            /* 'UncompressedEntry' header has already been read, recover the peek size. */
            peekSize -= (UINT32)sizeof(UncompressedEntry);
            break;
        }
#endif
        argsInfo.timestampBytes = OsHplCalcTimestampBytes(fullyTimestamp, ue.timestamp);
        argsInfo.totalSize = (UINT32)sizeof(HplCompressedArgsEntry) + argsInfo.timestampBytes +
                             (ue.entrySize - (UINT32)sizeof(UncompressedEntry));
        argsInfo.entry = &ue;
        argsInfo.stageBuf = stageBuf;

        /* When the buffer free size is not enough for the following storing, write metadata to file immediately. */
        if (freeSize < argsInfo.totalSize) {
            if (OsHplMetadataRecord(logger, writePos, size - freeSize) != LOS_OK) {
                HPL_DEBUG_PRINT("Pre arguments record failed\n");
                /* 'UncompressedEntry' header has already been read, recover the peek size. */
                peekSize -= (UINT32)sizeof(UncompressedEntry);
                break;
            }
            OsHplRingbufReadPeekDone(&stageBuf->buffer, peekSize);
            peekSize = fullyTimestamp = 0;
            argsFragment = OsHplCreateArgsFragment(writePos);
            wrAddr = writePos + (UINT32)sizeof(HplArgsFragment);
            freeSize = size - (UINT32)sizeof(HplArgsFragment);
            /* Recalculate timestamp bytes because of the change of fullyTimestamp. */
            UINT32 timeBytes = OsHplCalcTimestampBytes(fullyTimestamp, ue.timestamp);
            argsInfo.totalSize = argsInfo.totalSize - argsInfo.timestampBytes + timeBytes;
            argsInfo.timestampBytes = timeBytes;
        }

        if (OsHplCompressWriteArgs(&wrAddr, &freeSize, &argsInfo, fullyTimestamp, &peekSize) != LOS_OK) {
            HPL_DEBUG_PRINT("Arguments compress failed\n");
            continue;
        }

        argsFragment->argsDataBytes += argsInfo.totalSize;
        argsFragment->totalArgsEntrys++;
        fullyTimestamp = fullyTimestamp ? fullyTimestamp : ue.timestamp;
    }

    if (((size - freeSize) > 0) &&
        (OsHplMetadataRecord(logger, writePos, size - freeSize) != LOS_OK)) {
        HPL_DEBUG_PRINT("Final arguments record failed\n");
        return LOS_NOK;
    }
    OsHplRingbufReadPeekDone(&stageBuf->buffer, peekSize);
    return LOS_OK;
}

STATIC UINT32 OsHplCopmressArgs(HplRuntimeLogger *logger, UINT8 *writePos, UINT32 size, UINT32 taskId)
{
    HplHandle                   *hpl = NULL;

    LOS_DL_LIST_FOR_EACH_ENTRY(hpl, &logger->head, HplHandle, listNode) {
        if ((taskId != HPL_COMPRESS_ALL_TASK) && (hpl->hplTaskId != taskId)) {
            continue;
        }

        if (hpl->inited && (OsHplRingbufUsedSize(&hpl->stageBuf.buffer) != 0) &&
            (OsHplCompressTaskArgs(logger, writePos, size, hpl) != LOS_OK)) {
            break;
        }
        if ((hpl->preDelete == TRUE) && (OsHplRingbufUsedSize(&hpl->stageBuf.buffer) == 0)) {
            OsHplDelete(hpl);
        }
    }
    return LOS_OK;
}

STATIC UINT32 OsHplWriteCheckpoint(HplRuntimeLogger *logger)
{
    UINT32           ret;
    HplCheckPoint     ck;

    ck.magic = HPL_CHECKPOINT_MAGIC;
    ck.version = HPL_CHECKPOINT_VERSION;

    /* Write check point first */
    ret = OsHplMetadataRecord(logger, (UINT8 *)&ck, (UINT32)sizeof(HplCheckPoint));
    if (ret != LOS_OK) {
        return ret;
    }
    logger->checkpointFlag = TRUE;
    return ret;
}

STATIC VOID OsHplDoCompressLog(HplRuntimeLogger *logger, UINT8 *writePos, UINT32 size, UINT32 targets)
{
    UINT32 ret;
    ret = LOS_MuxPend(logger->compressMux, LOS_NO_WAIT);
    if (ret != LOS_OK) {
        return;
    }

    if ((!logger->checkpointFlag) && (OsHplWriteCheckpoint(logger) != LOS_OK)) {
        goto ERR;
    }
    /* Format and arguments are stored separately. */
#ifndef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
    (VOID)OsHplCompressFormat(logger, writePos, size);
#endif
    OsHplAbandonedlogRecord(logger, writePos, size);
    (VOID)OsHplCopmressArgs(logger, writePos, size, targets);

ERR:
    (VOID)LOS_MuxPost(logger->compressMux);
}

/* Compress the all tasks log. */
VOID OsHplCompressLog(VOID)
{
    OsHplDoCompressLog(&g_runtimeLogger, g_runtimeLogger.compressBuf, HPL_COMPRESS_ARGS_BUF_SIZE,
                       HPL_COMPRESS_ALL_TASK);
#ifdef LOSCFG_DEBUG_HPL_EMULATION
    (VOID)LOS_EventWrite(&g_runtimeLogger.event, HPL_COMPRESS_DONE);
#endif
}

/* Idle task has the lowest priority which interrupted may cause the other task compression failed,
 * because idle task already obtian compress mutex. So we make sure idle task compression undispatchable. */
VOID OsHplIdleCompressLog(VOID)
{
#if LOSCFG_KERNEL_HPL_IDLE_TASK_COMPRESSION
    LOS_TaskLock();
    OsHplCompressLog();
    LOS_TaskUnlock();
#endif
}

/* Be invoked in exception to compress all the remaining log. */
VOID OsHplExcCompressLog(VOID)
{
#ifndef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
    (VOID)OsHplCompressFormat(&g_runtimeLogger, g_runtimeLogger.compressBuf, HPL_COMPRESS_ARGS_BUF_SIZE);
#endif
    OsHplAbandonedlogRecord(&g_runtimeLogger, g_runtimeLogger.compressBuf, HPL_COMPRESS_ARGS_BUF_SIZE);
    (VOID)OsHplCopmressArgs(&g_runtimeLogger, g_runtimeLogger.compressBuf, HPL_COMPRESS_ARGS_BUF_SIZE,
                            HPL_COMPRESS_ALL_TASK);
}

#ifdef LOSCFG_OBSOLETE_API
VOID *OsHplCompressMain(UINTPTR priv,  UINTPTR param1,  UINTPTR param2,  UINTPTR param3)
#else
VOID *OsHplCompressMain(VOID *priv)
#endif
{
    UINT32           ret;
    HplRuntimeLogger *logger = (HplRuntimeLogger *)priv;
    UINT8            *compressBuf = NULL;

#ifdef LOSCFG_OBSOLETE_API
    (VOID)param1;
    (VOID)param2;
    (VOID)param3;
#endif
    compressBuf = logger->compressBuf;
    if (compressBuf == NULL) {
        HPL_DEBUG_PRINT("Compress buf NULL\n");
        return NULL;
    }

    while (1) {
        ret = LOS_EventRead(&logger->event, HPL_TRIGGER_EVENT | HPL_COMPRESS_MAIN_EXIT,
            LOS_WAITMODE_OR | LOS_WAITMODE_CLR, LOS_MS2Tick(HPL_TASK_SLEEP_MS));
        if (ret & HPL_COMPRESS_MAIN_EXIT) {
            break;
        }
        OsHplCompressLog();
    }
    return NULL;
}
