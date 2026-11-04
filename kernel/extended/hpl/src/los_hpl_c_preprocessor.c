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

#include "los_hpl.h"
#include "los_hpl_logger_pri.h"
#include "los_hpl_compression_pri.h"
#include "los_hpl_c_preprocessor_pri.h"
#include "los_memory.h"

/*
 * LiteOS high performance log has two kinds of preprocessor,
 * C language implementation and Python language implementation.
 */

STATIC BOOL OsHplGetDynamicNum(const CHAR *fmt, UINT32 *fmtIdx, UINT32 *dynNum)
{
    UINT32 idx = *fmtIdx;
    UINT32 dynamicNum = 0;
    BOOL hasWidthOrPrecisonFlag = FALSE;

    if (fmt[idx] == '#') {
        idx++;
        hasWidthOrPrecisonFlag = TRUE;
    }
    /* Check dynamic or static width. */
    if (fmt[idx] == '*') {
        idx++;
        dynamicNum++;
        hasWidthOrPrecisonFlag = TRUE;
    } else {
        if (fmt[idx] == '-') {
            idx++;
        }
        while ((fmt[idx] >= '0') && (fmt[idx] <= '9')) {
            idx++;
            hasWidthOrPrecisonFlag = TRUE;
        }
    }
    /* Check dynamic or static precision. */
    if (fmt[idx] == '.') {
        idx++;
        if (fmt[idx] == '*') {
            dynamicNum++;
            hasWidthOrPrecisonFlag = TRUE;
            idx++;
        } else {
            while ((fmt[idx] >= '0') && (fmt[idx] <= '9')) {
                idx++;
                hasWidthOrPrecisonFlag = TRUE;
            }
        }
    }
    *fmtIdx = idx;
    *dynNum = dynamicNum;
    return hasWidthOrPrecisonFlag;
}

STATIC VOID OsHplArchDynamicWidthArgs(UINT32 dynamicNum, UINT32 *argsNums, UINT64 *argsType)
{
    OsHplSetArgsType(dynamicNum, argsNums, (sizeof(UINTPTR) == sizeof(UINT32)) ? ARGS_TYPE_4_BYTES :
                     ARGS_TYPE_8_BYTES, argsType);
}

STATIC VOID OsHplAnalysisLongTypeArgs(const CHAR *fmt, UINT32 *idx, UINT32 dynamicNum, UINT32 *argsNums,
                                      UINT64 *argsType)
{
    if ((fmt[*idx] == 'l') && ((fmt[*idx + 1] == 'x') || (fmt[*idx + 1] == 'd') || (fmt[*idx + 1] == 'u') ||
        (fmt[*idx + 1] == 'X'))) {
        OsHplSetArgsType(dynamicNum, argsNums, ARGS_TYPE_8_BYTES, argsType);
        (*idx) += 2; /* 2 characters */
    } else if ((fmt[*idx] == 'x') || (fmt[*idx] == 'd') || (fmt[*idx] == 'u') || (fmt[*idx] == 'X')) {
        OsHplArchDynamicWidthArgs(dynamicNum, argsNums, argsType);
        (*idx)++;
    } else if ((fmt[*idx] == 'f')) {
        OsHplSetArgsType(dynamicNum, argsNums, ARGS_TYPE_DOUBLE, argsType);
        (*idx)++;
    }
}

STATIC INLINE UINT32 OsHplGetArgsTypeNum(const CHAR *fmt, UINT64 *analysis)
{
    UINT32 argsNums = 0;
    UINT64 argsType = 0;
    UINT32 idx = 0;
    UINT32 dynamicNum = 0;
    BOOL hasWidthOrPrecison = FALSE;

    while (*fmt != 0) {
        if (*fmt != '%') {
            fmt++;
            continue;
        }
        idx = idx ? idx : 1;
        /* Treat '%  x' as '%x' to match with standard print. */
        while (fmt[idx] == ' ') {
            idx++;
        }
        switch (fmt[idx]) {
            case 'd':
            case 'x':
            case 'u':
            case 'c':
            case 'X':
                OsHplSetArgsType(dynamicNum, &argsNums, ARGS_TYPE_4_BYTES, &argsType);
                idx++;
                break;
            case 'f':
                OsHplSetArgsType(dynamicNum, &argsNums, ARGS_TYPE_DOUBLE, &argsType);
                idx++;
                break;
            case 'p':
                OsHplArchDynamicWidthArgs(dynamicNum, &argsNums, &argsType);
                idx++;
                break;
            case 's':
                OsHplSetArgsType(dynamicNum, &argsNums, ARGS_TYPE_STRING, &argsType);
                idx++;
                break;
            case 'l':
                idx++;
                OsHplAnalysisLongTypeArgs(fmt, &idx, dynamicNum, &argsNums, &argsType);
                break;
            default:
                /* Check if format has dynamic width or precision. if already has dynamic width or precision,
                 * it indicates that it is not a illegal dynamic width or precision. such as "%*.*kkkkk". */
                if (hasWidthOrPrecison) {
                    break;
                }

                /* If has dynamic width or precision, we need continue to verify the following character. */
                hasWidthOrPrecison = OsHplGetDynamicNum(fmt, &idx, &dynamicNum);
                if (hasWidthOrPrecison) {
                    continue;
                }
                idx++;
        }
        fmt += idx;
        idx = 0;
        dynamicNum = 0;
        hasWidthOrPrecison = FALSE;
    }
    if (argsNums > HPL_MAX_ARGS_NUM) {
        return LOS_NOK;
    }
    HPL_SET_ARGS_TYPE_AND_NUM(argsNums, argsType, *analysis);
    return LOS_OK;
}

UINT32 OsHplAssistant(volatile UINT32 *fmtId, UINT64 *analysis, OsHplStaticInfo *fileInfo, const CHAR *format)
{
    UINT32 intSave;
    HplStaticInfo staticLog;
    UINT32 wrSize = (UINT32)sizeof(HplStaticInfo);
    UINT32 ret;

    if (*fmtId != LOS_HPL_UNASSIGNED_LOGID) {
        return LOS_OK;
    }

    ret = OsHplGetArgsTypeNum(format, analysis);
    if (ret != LOS_OK) {
        *analysis = LOS_HPL_INVALID_ANALYSIS;
        return ret;
    }
    staticLog.magic = HPL_UNCOMPRESSED_FMT_MAGIC;
    staticLog.file = fileInfo->fileName;
    staticLog.func = fileInfo->funcName;
    staticLog.line = fileInfo->line;
    staticLog.format = format;
    staticLog.fmtAnalysis = *analysis;

    /* The total string size con't exceed the half of the max compress buf size. */
    if ((strlen(staticLog.file) + strlen(staticLog.func) + strlen(format)) > (HPL_COMPRESS_ARGS_BUF_SIZE >> 1)) {
        *analysis = LOS_HPL_INVALID_ANALYSIS;
        return LOS_NOK;
    }

    /* Spin lock to protect the following ringbuf writing and fmtId unbreakable.
       So that the format info saved in order of fmtId. */
    LOS_SpinLockSave(&g_runtimeLogger.lock, &intSave);
    if (OsHplRingbufWrite(&g_runtimeLogger.fmtRingbuf, (const CHAR *)&staticLog, wrSize) != wrSize) {
        LOS_SpinUnlockRestore(&g_runtimeLogger.lock, intSave);
        OsHplRecordAbandonedLog(HPL_ABANDON_MAX_FMT_ID, LOS_SysCycleGet());
        return LOS_NOK;
    }
    *fmtId = g_runtimeLogger.maxFmtId;
    if (*fmtId > HPL_MAX_FMT_ID) {
        LOS_SpinUnlockRestore(&g_runtimeLogger.lock, intSave);
        return LOS_NOK;
    }
    g_runtimeLogger.maxFmtId++;
    /* C preprocessor do not need to distinguish module and treat all log as the same module. */
    HPL_SET_FMTID(*fmtId, HPL_SYSTEM_MODULE_ID);
    LOS_SpinUnlockRestore(&g_runtimeLogger.lock, intSave);
    return LOS_OK;
}

UINT32 OsHplCPreprocessInit(VOID)
{
    CHAR *fmtBuf = NULL;

    fmtBuf = LOS_MemAlloc(m_aucSysMem0, HPL_LOGGER_FMT_BUF_SIZE);
    if (fmtBuf == NULL) {
        return LOS_NOK;
    }
    OsHplRingbufInit(&g_runtimeLogger.fmtRingbuf, fmtBuf, HPL_LOGGER_FMT_BUF_SIZE);
    return LOS_OK;
}

VOID OsHplCPreprocessDeinit(VOID)
{
    (VOID)LOS_MemFree(m_aucSysMem0, g_runtimeLogger.fmtRingbuf.fifo);
}

STATIC UINT32 OsHplCompressWriteFormat(UINT8 **writeAddr, UINT32 *totalSize, HplStaticInfo *staticLog,
                                       HplFormatBytes *bytesInfo)
{
    HplCompressedFormatEntry *format = NULL;
    UINT8 *wrAddr = *writeAddr;
    UINT32 freeSize = *totalSize;
    errno_t err;

    format = (HplCompressedFormatEntry *)wrAddr;
    format->fmtEntryMagic = HPL_FMT_ENTRY_MAGIC;
    format->entryType = ENTRY_TYPE_FORMAT_ENTRY;
    format->filenameLength = bytesInfo->fileBytes;
    format->funcStringLength = bytesInfo->funcBytes;
    format->formatStringLength = bytesInfo->formatBytes;
    format->linenum = staticLog->line;
    format->fmtAnalysisBytes = bytesInfo->analysisBytes;
    wrAddr += (UINT32)sizeof(HplCompressedFormatEntry);
    freeSize -= (UINT32)sizeof(HplCompressedFormatEntry);

    err = memcpy_s(wrAddr, freeSize, &staticLog->fmtAnalysis, format->fmtAnalysisBytes);
    if (err != EOK) {
        return LOS_NOK;
    }
    wrAddr += format->fmtAnalysisBytes;
    freeSize -= format->fmtAnalysisBytes;

    err = memcpy_s(wrAddr, freeSize, staticLog->file, bytesInfo->fileBytes);
    if (err != EOK) {
        return LOS_NOK;
    }
    wrAddr += bytesInfo->fileBytes;
    freeSize -= bytesInfo->fileBytes;

    err = memcpy_s(wrAddr, freeSize, staticLog->func, bytesInfo->funcBytes);
    if (err != EOK) {
        return LOS_NOK;
    }
    wrAddr += bytesInfo->funcBytes;
    freeSize -= bytesInfo->funcBytes;

    err = memcpy_s(wrAddr, freeSize, staticLog->format, bytesInfo->formatBytes);
    if (err != EOK) {
        return LOS_NOK;
    }
    wrAddr += bytesInfo->formatBytes;
    freeSize -= bytesInfo->formatBytes;

    *writeAddr = wrAddr;
    *totalSize = freeSize;
    return LOS_OK;
}

UINT32 OsHplCompressFormat(HplRuntimeLogger *logger, UINT8 *writePos, UINT32 size)
{
    UINT8 *wrAddr = NULL;
    HplStaticInfo staticLog;
    UINT32 neededSize, freeSize, rdSize;
    UINT32 staticSize = (UINT32)sizeof(HplStaticInfo);
    UINT32 peekSize = 0;
    HplFormatBytes formatByteInfo;

    wrAddr = writePos;
    freeSize = size;

    while (OsHplRingbufPeekUsedSize(&logger->fmtRingbuf, peekSize) != 0) {
        rdSize = OsHplRingbufPeekRead(&logger->fmtRingbuf, (CHAR *)&staticLog, staticSize, peekSize);
        if ((rdSize != staticSize) || (staticLog.magic != HPL_UNCOMPRESSED_FMT_MAGIC)) {
            HPL_DEBUG_PRINT("fmt read magic failed\n");
            peekSize++;
            continue;
        }
        peekSize += rdSize;
        formatByteInfo.fileBytes = strlen(staticLog.file) + 1;
        formatByteInfo.formatBytes = strlen(staticLog.format) + 1;
        formatByteInfo.funcBytes = strlen(staticLog.func) + 1;
        formatByteInfo.analysisBytes = OsHplCalcBytes(staticLog.fmtAnalysis);
        neededSize = (UINT32)sizeof(HplCompressedFormatEntry) + formatByteInfo.analysisBytes +
                     formatByteInfo.fileBytes + formatByteInfo.formatBytes + formatByteInfo.funcBytes;
        /* When the buffer free size is not enough for the follow storing, writing metadata to file immediately. */
        if (freeSize < neededSize) {
            if (OsHplMetadataRecord(logger, writePos, size - freeSize) != LOS_OK) {
                HPL_DEBUG_PRINT("format metadata record failed\n");
                return LOS_NOK;
            }
            OsHplRingbufReadPeekDone(&logger->fmtRingbuf, peekSize);
            peekSize = 0;
            wrAddr = writePos;
            freeSize = size;
            if (freeSize < neededSize) {
                continue;
            }
        }

        if (OsHplCompressWriteFormat(&wrAddr, &freeSize, &staticLog, &formatByteInfo) != LOS_OK) {
            break;
        }

        logger->fmtRecordCnt++;
    }

    if (((size - freeSize) > 0) &&
        (OsHplMetadataRecord(logger, writePos, size - freeSize) != LOS_OK)) {
        HPL_DEBUG_PRINT("format metadata record failed\n");
        return LOS_NOK;
    }
    OsHplRingbufReadPeekDone(&logger->fmtRingbuf, peekSize);
    return LOS_OK;
}
