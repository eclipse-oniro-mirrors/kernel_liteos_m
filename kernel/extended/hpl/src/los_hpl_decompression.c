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
#include "securec.h"
#include "los_hpl_compression_pri.h"
#include "los_hpl_common_pri.h"
#include "los_hpl_decompression_pri.h"
#ifdef LOSCFG_DEBUG_HPL_EMULATION
#include "los_hpl_emulation_pri.h"
#endif

typedef struct {
    char *formatString;
    char *filename;
    uint32_t line;
    char *func;
    uint64_t analysis;
} OsHplDecompressStaticInfo;

HplDecompressor g_decompressor = {0};

#define PRINT_MACRO_HELPER(x)   #x
#define PRINT_MACRO(x)          #x "=" PRINT_MACRO_HELPER(x)
#if LOSCFG_KERNEL_HPL_CORE_BIT != 32 && LOSCFG_KERNEL_HPL_CORE_BIT != 64
    #pragma message(PRINT_MACRO(LOSCFG_KERNEL_HPL_CORE_BIT))
    #error "Invalid core bit!"
#endif

uint32_t OsHplSaveFormat(HplDecompressor *decompressor, uint32_t moduleId, HplDecompressFormat *decompressFormat)
{
    uintptr_t *bk = NULL;
    uintptr_t *new = NULL;
    uint32_t newSize;
    errno_t err;
    uint32_t fmtId = decompressFormat->fmtId;
    uint32_t needCnt = (fmtId + 1 + HPL_DECOMPRESS_ARRAY_NUM - 1) / HPL_DECOMPRESS_ARRAY_NUM * HPL_DECOMPRESS_ARRAY_NUM;

    /* Format array is not enough to store, realloc new buffer. */
    if (fmtId >= decompressor->arraytotalNum[moduleId]) {
        bk = decompressor->formatArray[moduleId];
        newSize = needCnt * (uint32_t)sizeof(uintptr_t);
        new = HPL_DECOMPRESS_MALLOC(newSize);
        if (new == NULL) {
            HPL_DECOMPRESS_DEBUG("Realloc formatArray memory failed\n");
            return LOS_NOK;
        }
        (VOID)memset(new, 0, newSize);
        if (bk != NULL) {
            err = memcpy_s(new, newSize, bk, decompressor->arraytotalNum[moduleId] * (uint32_t)sizeof(uintptr_t));
            if (err != EOK) {
                HPL_DECOMPRESS_FREE(new);
                return LOS_NOK;
            }
            HPL_DECOMPRESS_FREE(bk);
        }
        decompressor->formatArray[moduleId] = new;
        decompressor->arraytotalNum[moduleId] = needCnt;
    }

    if (decompressor->formatArray[moduleId][fmtId] != 0) {
        HPL_DECOMPRESS_DEBUG("Metadata has same moduleid and fmtid, %x, %x\n", moduleId, fmtId);
        return LOS_NOK;
    }
    decompressor->formatArray[moduleId][fmtId] = (uintptr_t)decompressFormat;

    decompressor->arrayUsed[moduleId]++;
    return LOS_OK;
}

STATIC VOID OsHplOutputString(char *log)
{
    HPL_DECOMPRESS_PRINT("%s", log);
}

#if !defined(LOSCFG_DEBUG_HPL_EMULATION) && defined(LOSCFG_KERNEL_HPL_TASK_LOCK_FREE)
int32_t logSortCmp(const VOID *element1, const VOID *element2)
{
    HplDecompressSortLog *p1 = (HplDecompressSortLog *)element1;
    HplDecompressSortLog *p2 = (HplDecompressSortLog *)element2;
    return p1->timestamp > p2->timestamp;
}

STATIC VOID OsHplDecompressLogSort(HplDecompressSortLog *logList, uint32_t listLength)
{
    uint32_t i;
    if (listLength == 0) {
        return;
    }
    qsort(logList, listLength, (int32_t)sizeof(HplDecompressSortLog), logSortCmp);
    for (i = 0; i < listLength; i++) {
        OsHplOutputString(logList[i].log);
        HPL_DECOMPRESS_FREE(logList[i].log);
    }
}

STATIC VOID OshplDecompressLogSave(HplDecompressor *decompressor, uint64_t timestamp, char *logString,
                                   uint32_t stringSize)
{
    HplDecompressSortLog *bk = NULL;
    HplDecompressSortLog *new = NULL;
    uint32_t newSize;
    errno_t err;

    if (decompressor->sortListUsed == decompressor->sortListTotalNum) {
        bk = decompressor->sortList;
        newSize = (decompressor->sortListTotalNum + HPL_DECOMPRESS_SORT_LIST_NUM) *
                  (uint32_t)sizeof(HplDecompressSortLog);
        new = HPL_DECOMPRESS_MALLOC(newSize);
        if (new == NULL) {
            HPL_DECOMPRESS_DEBUG("Realloc sortlist memory failed\n");
            return;
        }
        if (bk != NULL) {
            err = memcpy_s(new, newSize, bk, decompressor->sortListTotalNum * (uint32_t)sizeof(HplDecompressSortLog));
            if (err != EOK) {
                HPL_DECOMPRESS_FREE(new);
                return;
            }
            HPL_DECOMPRESS_FREE(bk);
        }
        decompressor->sortList = new;
        decompressor->sortListTotalNum += HPL_DECOMPRESS_SORT_LIST_NUM;
    }

    decompressor->sortList[decompressor->sortListUsed].timestamp = timestamp;
    decompressor->sortList[decompressor->sortListUsed].log = HPL_DECOMPRESS_MALLOC(stringSize);
    if ((decompressor->sortList[decompressor->sortListUsed].log == NULL) ||
        (memcpy_s(decompressor->sortList[decompressor->sortListUsed].log, stringSize, logString, stringSize) != EOK)) {
        HPL_DECOMPRESS_DEBUG("Copy string failed\n");
        return;
    }
    decompressor->sortList[decompressor->sortListUsed].logSize = stringSize;
    decompressor->sortListUsed++;
}
#endif

STATIC uint32_t OsHplDecompressUserCustomLog(HplDecompressor *decompressor, char *buf, uint32_t *entrySize)
{
    HplCustomStringHeader *userString = (HplCustomStringHeader *)buf;
    uint32_t stringSize = (uint32_t)userString->dataLength + 1; /* include '\0' */
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
    uint64_t timestamp = userString->timestamp;
#else
    uint64_t timestamp = 0;
#endif
    if (stringSize > decompressor->customStrSize) {
        if (decompressor->customString != NULL) {
            HPL_DECOMPRESS_FREE(decompressor->customString);
        }
        decompressor->customString = HPL_DECOMPRESS_MALLOC(stringSize);
        if (decompressor->customString == NULL) {
            HPL_DECOMPRESS_DEBUG("Alloc custom string failed\n");
            decompressor->customStrSize = 0;
            return LOS_NOK;
        }
        decompressor->customStrSize = stringSize;
    }

    if (memcpy_s(decompressor->customString, decompressor->customStrSize, userString->data,
                 userString->dataLength) != LOS_OK) {
        HPL_DECOMPRESS_DEBUG("memcpy custom string failed\n");
        return LOS_NOK;
    }
    decompressor->customString[userString->dataLength] = '\0';
#if defined(LOSCFG_DEBUG_HPL_EMULATION) || !defined(LOSCFG_KERNEL_HPL_TASK_LOCK_FREE)
    (VOID)timestamp;
    OsHplOutputString(decompressor->customString);
#else
    OshplDecompressLogSave(decompressor, timestamp, decompressor->customString,
                           decompressor->customStrSize); /* include '\0' */
#endif
    *entrySize = (uint32_t)sizeof(HplCustomStringHeader) + userString->dataLength;
    return LOS_OK;
}

/* Output the log that abandoned. */
STATIC uint32_t OsHplDecompressAbandonLog(HplDecompressor *decompressor, char *buf, uint32_t *entrySize)
{
    HplAbandonedLogEntry *abandonedEntry = (HplAbandonedLogEntry *)buf;
    uint32_t abandonNum;
    HplDecompressFormat *decompressFormat = NULL;
    char *filename = NULL;
    char  *fmtSrting = NULL;
    uint32_t abandonSize;
    uint32_t i;
    uint32_t formatId, moduleId;
    HplAbandonedLog *abandonLog;

    abandonNum = abandonedEntry->abandonNum;
    abandonSize = (uint32_t)sizeof(HplAbandonedLogEntry);
    for (i = 0; i < abandonNum; i++) {
        abandonLog = &abandonedEntry->abandonInfo[i];
        if (abandonLog->fmtId == HPL_ABANDON_MAX_FMT_ID) {
            HPL_DECOMPRESS_PRINT("HPL abandon format\n");
            abandonSize += (uint32_t)sizeof(HplAbandonedLog);
            continue;
        }
        formatId = HPL_GET_FMT_ID(abandonLog->fmtId);
        moduleId = HPL_GET_MODULE_ID(abandonLog->fmtId);
        if (moduleId >= HPL_MAX_MODULE_NUM) {
            HPL_DECOMPRESS_DEBUG("Invalid abandoned log module id %u\n", moduleId);
            return LOS_NOK;
        }
        if (formatId >= decompressor->arrayUsed[moduleId]) {
            HPL_DECOMPRESS_DEBUG("There no format string for this log %u, %u\n", formatId,
                                 decompressor->arrayUsed[moduleId]);
            abandonSize += (uint32_t)sizeof(HplAbandonedLog);
            continue;
        }
        decompressFormat = (HplDecompressFormat *)decompressor->formatArray[moduleId][formatId];

        filename = (char *)((uint8_t *)decompressFormat + (uint32_t)sizeof(HplDecompressFormat));
        fmtSrting = (char *)((uint8_t *)decompressFormat + decompressFormat->filenameBytes +
                    decompressFormat->funcBytes + (uint32_t)sizeof(HplDecompressFormat));

#if defined(LOSCFG_DEBUG_HPL_EMULATION) || !defined(LOSCFG_KERNEL_HPL_TASK_LOCK_FREE)
        HPL_DECOMPRESS_PRINT("HPL abandon log [%s:%u] %s\n", filename, decompressFormat->linenum, fmtSrting);
#else
        char fmtString[HPL_DECOMPRESS_MAX_FMT_SIZE];
        int32_t len = snprintf_s(fmtString, HPL_DECOMPRESS_MAX_FMT_SIZE, HPL_DECOMPRESS_MAX_FMT_SIZE - 1,
                                 "HPL abandon log [%#llx][%s:%u] %s\n", abandonLog->timestamp, filename,
                                 decompressFormat->linenum, fmtSrting);
        if (len == -1) {
            HPL_DECOMPRESS_DEBUG("abandon log snprintf failed %u\n", formatId);
            continue;
        }
        fmtString[len] = '\0';
        OshplDecompressLogSave(decompressor, abandonLog->timestamp, fmtString, strlen(fmtString) + 1);
#endif
        abandonSize += (uint32_t)sizeof(HplAbandonedLog);
    }
    *entrySize = abandonSize;
    return LOS_OK;
}

STATIC uint32_t OsHplDecompressFormat(HplDecompressor *decompressor, char *buf, uint32_t *entrySize)
{
    HplCompressedFormatEntry    *fe = NULL;
    HplDecompressFormat         *decompressFormat = NULL;
    uint32_t formatSize = 0;
    uint32_t stringSize;
    uint32_t ret;
    errno_t err;

    fe = (HplCompressedFormatEntry *)buf;
    if (fe->fmtEntryMagic != HPL_FMT_ENTRY_MAGIC) {
        HPL_DECOMPRESS_DEBUG("Decompressor wrong format entry %u\n", fe->entryType);
        return LOS_NOK;
    }

    stringSize = fe->filenameLength + fe->formatStringLength + fe->funcStringLength;
    decompressFormat = HPL_DECOMPRESS_MALLOC(stringSize + (uint32_t)sizeof(HplDecompressFormat));
    if (decompressFormat == NULL) {
        HPL_DECOMPRESS_DEBUG("Alloc memory failed\n");
        return LOS_NOK;
    }
    decompressFormat->filenameBytes = fe->filenameLength;
    decompressFormat->formatStringBytes = fe->formatStringLength;
    decompressFormat->funcBytes = fe->funcStringLength;
    decompressFormat->linenum = fe->linenum;
    decompressFormat->fmtAnalysis = 0;
    decompressFormat->fmtId = decompressor->arrayUsed[HPL_SYSTEM_MODULE_ID];

    err = memcpy_s(&decompressFormat->fmtAnalysis, (uint32_t)sizeof(uint64_t),
                   buf + (uint32_t)sizeof(HplCompressedFormatEntry), fe->fmtAnalysisBytes);
    if (err != EOK) {
        return LOS_NOK;
    }
    buf = buf + fe->fmtAnalysisBytes + (uint32_t)sizeof(HplCompressedFormatEntry);
    formatSize += fe->fmtAnalysisBytes + (uint32_t)sizeof(HplCompressedFormatEntry);

    err = memcpy_s(decompressFormat->data, stringSize, buf, stringSize);
    if (err != EOK) {
        return LOS_NOK;
    }
    buf = buf + stringSize;
    formatSize += stringSize;

    ret = OsHplSaveFormat(decompressor, HPL_SYSTEM_MODULE_ID, decompressFormat);
    if (ret != LOS_OK) {
        HPL_DECOMPRESS_DEBUG("Alloc memory failed\n");
        return ret;
    }

    *entrySize = formatSize;
    return LOS_OK;
}

STATIC size_t OsHplHasWidthOrPrecision(uint32_t *fmtIdx, const char *fmt, size_t *dynamicWidth,
                                       size_t *dynamicPrecision)
{
    uint32_t idx = *fmtIdx;
    size_t flag = FALSE;

    /* Check if format has dynamic width or precision. if already has dynamic width or precision, it indicates
     * that it is not a illegal dynamic width or precision. such as "%*.*kkkkk". */
    if ((*dynamicWidth == TRUE) || (*dynamicPrecision == TRUE)) {
        return FALSE;
    }

    if (fmt[idx] == '#') {
        idx++;
        flag = TRUE;
    }

    /* Check dynamic or static width. */
    if (fmt[idx] == '*') {
        idx++;
        *dynamicWidth = TRUE;
        flag = TRUE;
    } else {
        while ((fmt[idx] >= '0' && fmt[idx] <= '9') || (fmt[idx] == '-') || (fmt[idx] == '+')) {
            idx++;
            flag = TRUE;
        }
    }
    /* Check dynamic or static precision. */
    if (fmt[idx] == '.') {
        idx++;
        if (fmt[idx] == '*') {
            idx++;
            *dynamicPrecision = TRUE;
            flag = TRUE;
        } else {
            while ((fmt[idx] >= '0' && fmt[idx] <= '9')|| (fmt[idx] == '-') || (fmt[idx] == '+')) {
                idx++;
                flag = TRUE;
            }
        }
    }
    *fmtIdx = idx;
    return flag;
}

STATIC size_t OsHplIntegerType(char c)
{
    if ((c == 'x') || (c == 'd') || (c == 'u') || (c == 'X')) {
        return TRUE;
    }
    return FALSE;
}

uint32_t OsHplVprintfFmt(OsHplDecompressStaticInfo *staticInfo, char *pBuf, uint32_t size, uint64_t *arguments)
{
    uint32_t len = 0;
    uint32_t i = 0;
    uint32_t remain;
    size_t dynamicWidth = FALSE;
    size_t dynamicPrecision = FALSE;
    uint32_t idx = 0;
    const char *fmt = staticInfo->formatString;
    char fmtBuf[HPL_DECOMPRESS_FMT_BUF_SIZE];
    int32_t ret;

    while (*fmt != 0) {
        if (*fmt != '%') {
            pBuf[len++] = *fmt;
            fmt++;
            continue;
        }
        idx = idx ? idx : 1;
        remain = size - len;
        while (fmt[idx] == ' ') {
            idx++;
        }
        switch (fmt[idx]) {
            case 'd':
            case 'x':
            case 'X':
            case 'u':
            case 'c':
            case 'i':
                HPL_DECOMPRESS_INT_FMT();
                break;
            case 'j':                   /* '%j': add function name and line. */
                idx++;
                ret = snprintf_s(&pBuf[len], remain, remain - 1, "<%s,%d>", staticInfo->func, staticInfo->line);
                if (ret == -1) {
                    return 0;
                }
                HPL_DECOMPRESS_FMT_FINISH();
                break;
            case 'h': /* %h */
                idx++;
                if (fmt[idx] == 'h') {    /* %hhu %hhd */
                    idx++;
                }
                HPL_DECOMPRESS_INT_FMT();
                break;
            case 'z':
                idx++;
                HPL_DECOMPRESS_INT_FMT();
                break;
            case 'p':
            case 's':
                HPL_DECOMPRESS_DYNAMIC_INT_FMT();
                break;
            case 'g':
            case 'f':
                HPL_DECOMPRESS_FLOAT_FMT();
                break;
            case 'l':
                idx++;
                if (OsHplIntegerType(fmt[idx])) {
                    if (fmt[idx] == 'd') {
#if LOSCFG_KERNEL_HPL_CORE_BIT == 32
                        int64_t i64 = (int64_t)(int32_t)arguments[i];
                        arguments[i] = (uint64_t)i64;
#endif
                    }
                    HPL_DECOMPRESS_DYNAMIC_INT_FMT();
                } else if (fmt[idx] == 'f') {
                    HPL_DECOMPRESS_FLOAT_FMT();
                } else if (fmt[idx] == 'l') {
                    idx++;
                    if (OsHplIntegerType(fmt[idx])) {
                        HPL_DECOMPRESS_LONG_INT_FMT();
                    } else {
                        HPL_DECOMPRESS_INVALID_FMT();
                    }
                } else {
                    HPL_DECOMPRESS_INVALID_FMT();
                }
                break;
            default:
                if ((fmt[idx] == '%') && (idx == 1)) {
                    pBuf[len++] = fmt[idx];
                    /* Transferred meaning of '%%' 2 characters. */
                    fmt += 2;
                    idx = 0;
                    break;
                }
                /* If has width or precision type, we need continue; to verify the following character. */
                if (OsHplHasWidthOrPrecision(&idx, fmt, &dynamicWidth, &dynamicPrecision)) {
                    break;
                }
                /* Unidentifed characters. */
                HPL_DECOMPRESS_INVALID_FMT();
                break;
        }
    }
    return len;
}

STATIC uint32_t OsHplDecompressArgsType(uint32_t argsType, uint64_t *arguments, uint32_t idx, uint8_t **argsBuf)
{
    uint8_t *args = *argsBuf;
    char *string;

    if (argsType == ARGS_TYPE_4_BYTES) {
        if (memcpy_s(&arguments[idx], (uint32_t)sizeof(uint64_t), args, (uint32_t)sizeof(uint32_t)) != EOK) {
            return LOS_NOK;
        }
        args += (uint32_t)sizeof(uint32_t);
    } else if (argsType == ARGS_TYPE_8_BYTES) {
        if (memcpy_s(&arguments[idx], (uint32_t)sizeof(uint64_t), args, (uint32_t)sizeof(uint64_t)) != EOK) {
            return LOS_NOK;
        }
        args += (uint32_t)sizeof(uint64_t);
    } else if (argsType == ARGS_TYPE_STRING) {
        string = (char *)args;
        arguments[idx] = (uintptr_t)(string);
        args = args + (uint32_t)strlen((char *)args) + 1;
    }  else if (argsType == ARGS_TYPE_DOUBLE) {
        if (memcpy_s(&arguments[idx], (uint32_t)sizeof(uint64_t), args, (uint32_t)sizeof(DOUBLE)) != EOK) {
            return LOS_NOK;
        }
        args += (uint32_t)sizeof(DOUBLE);
    } else {
        HPL_DECOMPRESS_DEBUG("Invalid Arguments type %u\n", argsType);
        return LOS_NOK;
    }
    *argsBuf = args;
    return LOS_OK;
}

STATIC uint32_t OsHplDecompressAgentPrint(HplDecompressor *decompressor, uint64_t timestamp,
                                          OsHplDecompressStaticInfo *staticInfo, HplCompressedArgsEntry *ae)
{
    uint32_t i;
    uint32_t argsNum = HPL_GET_ARGS_NUM(staticInfo->analysis);
    uint64_t arguments[HPL_MAX_ARGS_NUM];
    uint32_t argsType;
    char pBuf[HPL_DECOMPRESS_MAX_FMT_SIZE];
    int32_t len = 0;
    uint8_t *args = NULL;

    if (argsNum > HPL_MAX_ARGS_NUM) {
        HPL_DECOMPRESS_DEBUG("Arguments number invalid %u, analysis %lx\n", argsNum, staticInfo->analysis);
        return LOS_NOK;
    }

    args = (uint8_t *)ae + (uint32_t)sizeof(HplCompressedArgsEntry) + ae->additionalTimeStampBytes;
    (void)memset_s(arguments, sizeof(uint64_t) * HPL_MAX_ARGS_NUM, 0, sizeof(uint64_t) * HPL_MAX_ARGS_NUM);
    for (i = 0; i < argsNum; i++) {
        argsType = HPL_NEXT_ARGS_TYPE(staticInfo->analysis, i);
        if (OsHplDecompressArgsType(argsType, arguments, i, &args) != LOS_OK) {
            return LOS_NOK;
        }
    }
#ifdef LOSCFG_KERNEL_HPL_STATIC_INFO
    /* 2: reserve 1 bytes for '\n' */
    int32_t ret = snprintf_s(&pBuf[len], HPL_DECOMPRESS_MAX_FMT_SIZE - len - 1, HPL_DECOMPRESS_MAX_FMT_SIZE - len - 2,
                             "[%s:%u] ", staticInfo->filename, staticInfo->line);
    if (ret == -1) {
        HPL_DECOMPRESS_DEBUG("Static info snprintf failed [%s, %u]\n", staticInfo->filename, staticInfo->line);
        return LOS_NOK;
    }
    len += ret;
#endif
    /* reserve 1 bytes for '\n' */
    len += (int32_t)OsHplVprintfFmt(staticInfo, &pBuf[len], HPL_DECOMPRESS_MAX_FMT_SIZE - (uint32_t)len - 1,
                                    arguments);
    pBuf[len] = '\0';
#ifdef LOSCFG_KERNEL_HPL_AUTO_LINEFEED
    pBuf[len] = '\n';
    pBuf[len + 1] = '\0';
#endif

#if defined(LOSCFG_DEBUG_HPL_EMULATION) || !defined(LOSCFG_KERNEL_HPL_TASK_LOCK_FREE)
    (VOID)decompressor;
    (VOID)timestamp;
    HPL_DECOMPRESS_PRINT("%s", pBuf);
#else
    OshplDecompressLogSave(decompressor, timestamp, pBuf, strlen(pBuf) + 1);
#endif
    return LOS_OK;
}

STATIC VOID OsHplDecompressGetFormat(HplDecompressor *decompressor, uint32_t moduleId, uint32_t formatId,
                                     uint64_t timestamp, HplCompressedArgsEntry *ae)
{
    HplDecompressFormat         *decompressFormat = NULL;
    OsHplDecompressStaticInfo   staticInfo;

    decompressFormat = (HplDecompressFormat *)decompressor->formatArray[moduleId][formatId];
    staticInfo.filename = (char *)((uint8_t *)decompressFormat + (uint32_t)sizeof(HplDecompressFormat));
    staticInfo.func = (char *)((uint8_t *)decompressFormat + decompressFormat->filenameBytes +
                (uint32_t)sizeof(HplDecompressFormat));
    staticInfo.formatString = (char *)((uint8_t *)decompressFormat + decompressFormat->filenameBytes +
                              decompressFormat->funcBytes + (uint32_t)sizeof(HplDecompressFormat));

    staticInfo.analysis = decompressFormat->fmtAnalysis;
    staticInfo.line = decompressFormat->linenum;

    (VOID)OsHplDecompressAgentPrint(decompressor, timestamp, &staticInfo, ae);
}

uint32_t OsHplDoDecompressArgument(HplDecompressor *decompressor, char *buf, uint32_t *entrySize,
                                   uint64_t *fullyTimeStamp)
{
    HplCompressedArgsEntry      *ae = NULL;
    uint32_t formatId, moduleId;
    uint64_t timestamp = 0;
    errno_t err;

    ae = (HplCompressedArgsEntry *)buf;
    if (ae->argsEntryMagic != HPL_ARGS_ENTRY_MAGIC) {
        HPL_DECOMPRESS_DEBUG("Decompressor wrong arguments entry %u\n", ae->argsEntryMagic);
        return LOS_NOK;
    }
    *entrySize = ae->totalEntryLength;
    if (ae->protocolVersion != HPL_CHECKPOINT_VERSION) {
        HPL_DECOMPRESS_DEBUG("Compressor and decompressor incompatible, log version %x, decmpress version %x\n",
                             ae->protocolVersion, HPL_CHECKPOINT_VERSION);
        return LOS_NOK;
    }
    formatId = HPL_GET_FMT_ID(ae->formatId);
    moduleId = HPL_GET_MODULE_ID(ae->formatId);
    if (moduleId >= HPL_MAX_MODULE_NUM) {
        HPL_DECOMPRESS_DEBUG("Invalid args module id %u\n", moduleId);
        return LOS_NOK;
    }

    if ((ae->additionalTimeStampBytes > 0) && (fullyTimeStamp != NULL)) {
        err = memcpy_s(&timestamp, (uint32_t)sizeof(uint64_t),
                       buf + (uint32_t)sizeof(HplCompressedArgsEntry), ae->additionalTimeStampBytes);
        if (err != EOK) {
            return LOS_NOK;
        }
        if (*fullyTimeStamp == 0) {
            *fullyTimeStamp = timestamp;
        } else {
            timestamp += *fullyTimeStamp;
        }
    }

    if (formatId >= decompressor->arrayUsed[moduleId]) {
        HPL_DECOMPRESS_DEBUG("There no format string for this args %x, %u\n", ae->formatId,
                             decompressor->arrayUsed[moduleId]);
        return LOS_OK;
    }

    OsHplDecompressGetFormat(decompressor, moduleId, formatId, timestamp, ae);
    return LOS_OK;
}

STATIC uint32_t OsHplDecompressArguments(HplDecompressor *decompressor, char *buf, uint32_t *entrySize)
{
    HplArgsFragment *af = (HplArgsFragment *)buf;
    uint32_t argSize;
    uint64_t fullyTimeStamp = 0;

    *entrySize = (uint32_t)sizeof(HplArgsFragment);
    for (uint32_t i = 0; i < af->totalArgsEntrys; i++) {
        if (OsHplDoDecompressArgument(decompressor, buf + *entrySize, &argSize, &fullyTimeStamp) != LOS_OK) {
            return LOS_NOK;
        }
        *entrySize += argSize;
    }
    return LOS_OK;
}

uint32_t OsHplDecompressInit(HplDecompressor *decompressor)
{
    uint32_t i, j;
    if (decompressor->init == TRUE) {
        for (i = 0; i < HPL_MAX_MODULE_NUM; i++) {
            for (j = 0; j < decompressor->arrayUsed[i]; j++) {
                HPL_DECOMPRESS_FREE((VOID *)decompressor->formatArray[i][j]);
            }
            decompressor->arrayUsed[i] = 0;
        }
        return LOS_OK;
    }
    return LOS_OK;
}

uint32_t OsHplDecompressLog(HplDecompressor *decompressor, char *buf, uint32_t size)
{
    uint32_t *type = NULL;
    HplCheckPoint *ck;
    uint32_t decompressSize = 0;
    uint32_t entrySize = 0;

    while (decompressSize < size) {
        type = (uint32_t *)buf;
        if (*type == HPL_CHECKPOINT_MAGIC) {
#if !defined(LOSCFG_DEBUG_HPL_EMULATION) && defined(LOSCFG_KERNEL_HPL_TASK_LOCK_FREE)
            /*
             * This indicate a new startup which assist for timestamp ordering.
             * sort and output the log in case of log file has multi startup log.
             */
            OsHplDecompressLogSort(decompressor->sortList, decompressor->sortListUsed);
            decompressor->sortListUsed = 0;
#endif
            ck = (HplCheckPoint *)buf;
            if (ck->version != HPL_CHECKPOINT_VERSION) {
                HPL_DECOMPRESS_DEBUG("Compressor and decompressor incompatible, log version %x\n", ck->version);
                return LOS_NOK;
            }
#ifndef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
            if (OsHplDecompressInit(decompressor) != LOS_OK) {
                return LOS_NOK;
            }
            decompressor->init = TRUE;
#endif
            entrySize = (uint32_t)sizeof(HplCheckPoint);
        } else if (decompressor->init) {
            switch (*type) {
                case HPL_FMT_ENTRY_MAGIC:
                    if ((OsHplDecompressFormat(decompressor, buf, &entrySize) != LOS_OK) || (entrySize == 0)) {
                        HPL_DECOMPRESS_DEBUG("decompress fmt failed 0x%x, %x\n", decompressSize, (uint8_t)*buf);
                        entrySize = 1;
                    }
                    break;
                case HPL_ARGS_FRAGMENT_MAGIC:
                    if ((OsHplDecompressArguments(decompressor, buf, &entrySize) != LOS_OK) || (entrySize == 0)) {
                        HPL_DECOMPRESS_DEBUG("decompress args fragment failed 0x%x, %x\n", decompressSize,
                                             (uint8_t)*buf);
                        entrySize = 1;
                    }
                    break;
                case HPL_ARGS_ENTRY_MAGIC:
                    if ((OsHplDoDecompressArgument(decompressor, buf, &entrySize, NULL) != LOS_OK) ||
                        (entrySize == 0)) {
                        HPL_DECOMPRESS_DEBUG("decompress args failed 0x%x, %x\n", decompressSize, (uint8_t)*buf);
                        entrySize = 1;
                    }
                    break;
                case HPL_ABANDONED_LOG_ENTRY_MAGIC:
                    if ((OsHplDecompressAbandonLog(decompressor, buf, &entrySize) != LOS_OK) || (entrySize == 0)) {
                        HPL_DECOMPRESS_DEBUG("decompress abandon log failed 0x%x, %x\n", decompressSize, (uint8_t)*buf);
                        entrySize = 1;
                    }
                    break;
                case HPL_USER_CUSTOM_STRING_MAGIC:
                    if ((OsHplDecompressUserCustomLog(decompressor, buf, &entrySize) != LOS_OK) || (entrySize == 0)) {
                        HPL_DECOMPRESS_DEBUG("decompress user custom string failed 0x%x, %x\n", decompressSize,
                                             (uint8_t)*buf);
                        entrySize = 1;
                    }
                    break;
                default:
                    entrySize = 1;
                    break;
            }
        } else {
            entrySize = 1;
        }
        decompressSize += entrySize;
        buf += entrySize;
    }
    return LOS_OK;
}

#ifndef LOSCFG_DEBUG_HPL_EMULATION
#ifdef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
#define STRING_MAX_LEN 1024
#define LINE_NUM_MAX_LEN (HPL_MAX_ARGS_NUM * 2)

STATIC uint32_t JumpNoteHeader(FILE *metaFp)
{
    char buf[STRING_MAX_LEN];
    while (1) {
        if (fgets(buf, STRING_MAX_LEN, metaFp) == NULL) {
            HPL_DECOMPRESS_DEBUG("File read note path failed\n");
            return LOS_NOK;
        }

        if (strstr(buf, "The analysis result of format string") != NULL) {
            /* Jump an empty line between note and format info. */
            if (fgets(buf, STRING_MAX_LEN, metaFp) == NULL) {
                HPL_DECOMPRESS_DEBUG("File read empty line failed\n");
                return LOS_NOK;
            }
            break;
        }
    }
    return LOS_OK;
}
/*
 * extra double qutoe: "Processor   : %s" " * %d\n"
 */
STATIC uint32_t ReplaceEscapeSymbolAndExtraDoubleQuote(const char *formatLine, char *outFormat, uint32_t OutMaxLen)
{
    uint32_t fmtIdx = 0;
    uint32_t outSize = 0;
    uint32_t i;
    char escapeformatIn[] = {'\?', '\'', '\"', '\\', '0', 'a', 'b', 'f', 'n', 'r', 't', 'v'};
    char escapeformatOut[] = {'\?', '\'', '\"', '\\', '\0', '\a', '\b', '\f', '\n', '\r', '\t', '\v'};

    while (formatLine[fmtIdx] != '\0') {
        if (outSize > OutMaxLen) {
            HPL_DECOMPRESS_DEBUG("Out format length overflow\n");
            return LOS_NOK;
        }
        /* Escape symbols */
        if (formatLine[fmtIdx] == '\\') {
            fmtIdx++;
            for (i = 0; i < ((uint32_t)sizeof(escapeformatIn) / (uint32_t)sizeof(char)); i++) {
                if (escapeformatIn[i] == formatLine[fmtIdx]) {
                    outFormat[outSize++] = escapeformatOut[i];
                    fmtIdx++;
                    break;
                }
            }
            if (i == ((uint32_t)sizeof(escapeformatIn) / (uint32_t)sizeof(char))) {
                outFormat[outSize++] = formatLine[fmtIdx++];
            }
        } else if (formatLine[fmtIdx] == '\"') { /* Ignore "\" \"" */
            fmtIdx++;
            while (formatLine[fmtIdx] == ' ') {
                fmtIdx++;
            }
            if (formatLine[fmtIdx] != '\"') {
                HPL_DECOMPRESS_DEBUG("Invalid format: %s\n", formatLine);
                return LOS_NOK;
            }
            fmtIdx++;
        } else {
            outFormat[outSize++] = formatLine[fmtIdx++];
        }
    }
    return LOS_OK;
}

STATIC uint32_t RecordMetadata(HplDecompressor *decompressor, char *pathLine, char *formatLine,
                               char *lineNum, char *analysisLine)
{
    char *endptr = NULL;
    uint32_t stringSize;
    uint32_t pathStringLen;
    uint32_t formatStringLen;
    uint32_t ret = 0;
    errno_t err;
    char lineFeedFormat[STRING_MAX_LEN] = {0};
    HplDecompressFormat *decompressFormat = NULL;
    const char separator[] = " ";
    char *token = NULL;
    uint32_t moduleId;
    uint32_t fmtResult;
    char *buf = NULL;
    uint32_t funcNameLen = 0;
    char *funcName = NULL;
    uint32_t line = 0;

    /* Skip double quote at the beginning of character string. */
    pathLine++;
    pathStringLen = (uint32_t)strlen(pathLine);
    /* 2 : Metadata file has double quote and line feed character at the end of line
     * which need to be replace with '\0' */
    pathLine[pathStringLen - 2] = '\0';
    pathStringLen -= 1; /* Length include '\0' */

    /* Skip double quote at the beginning of character string. */
    formatLine++;
    formatStringLen = (uint32_t)strlen(formatLine);
    /* 2 : Metadata file has double quote and line feed character at the end of line
     * which need to be replace with '\0' */
    formatLine[formatStringLen - 2] = '\0';
    formatStringLen -= 1; /* Length include '\0' */
    ret = ReplaceEscapeSymbolAndExtraDoubleQuote(formatLine, lineFeedFormat, STRING_MAX_LEN);
    if (ret != LOS_OK) {
        return ret;
    }

    /* Parse function and line info. */
    token = strtok_s(lineNum, separator, &buf);
    if (token != NULL) {
        funcName = token;
        funcNameLen = strlen(funcName) + 1; /* include '\0' */
    }

    token = strtok_s(NULL, separator, &buf);
    if (token != NULL) {
        line = (uint32_t)strtoul(token, &endptr, 0);
    }

    stringSize = pathStringLen + formatStringLen + funcNameLen;
    decompressFormat = HPL_DECOMPRESS_MALLOC(stringSize + (uint32_t)sizeof(HplDecompressFormat));
    if (decompressFormat == NULL) {
        HPL_DECOMPRESS_DEBUG("Alloc memory failed\n");
        return LOS_NOK;
    }
    decompressFormat->filenameBytes = pathStringLen;
    decompressFormat->formatStringBytes = formatStringLen;
    decompressFormat->funcBytes = funcNameLen;
    decompressFormat->linenum = line;
    token = strtok_s(analysisLine, separator, &buf);
    if (token == NULL) {
        HPL_DECOMPRESS_DEBUG("Get analysis failed\n");
        return LOS_NOK;
    }
    decompressFormat->fmtAnalysis = (uint64_t)strtoull(token, &endptr, 0);
    token = strtok_s(NULL, separator, &buf);
    if (token == NULL) {
        HPL_DECOMPRESS_DEBUG("Get fmtid failed\n");
        return LOS_NOK;
    }
    fmtResult = (uint32_t)strtoul(token, &endptr, 0);
    decompressFormat->fmtId = HPL_GET_FMT_ID(fmtResult);
    moduleId = HPL_GET_MODULE_ID(fmtResult);

    err = memcpy_s(decompressFormat->data, stringSize, pathLine, pathStringLen);
    if (err != EOK) {
        return LOS_NOK;
    }
    err = memcpy_s(decompressFormat->data + pathStringLen, stringSize - pathStringLen, funcName, funcNameLen);
    if (err != EOK) {
        return LOS_NOK;
    }
    err = memcpy_s(decompressFormat->data + pathStringLen + funcNameLen, stringSize - pathStringLen - funcNameLen,
                   lineFeedFormat, formatStringLen);
    if (err != EOK) {
        return LOS_NOK;
    }

    ret = OsHplSaveFormat(decompressor, moduleId, decompressFormat);
    if (ret != LOS_OK) {
        return LOS_NOK;
    }
    return LOS_OK;
}

STATIC uint32_t ReadMetadata(FILE *metaFp, HplDecompressor *decompressor)
{
    char pathLine[STRING_MAX_LEN];
    char formatLine[STRING_MAX_LEN];
    char lineNum[STRING_MAX_LEN];
    char analysisLine[LINE_NUM_MAX_LEN];

    while (!feof(metaFp)) {
        /* Get file path string. */
        if (fgets(pathLine, STRING_MAX_LEN, metaFp) == NULL) {
            break;
        }

        /* Get line num. */
        if (fgets(lineNum, STRING_MAX_LEN, metaFp) == NULL) {
            HPL_DECOMPRESS_DEBUG("File read line failed\n");
            return LOS_NOK;
        }

        /* Get format string. */
        if (fgets(formatLine, STRING_MAX_LEN, metaFp) == NULL) {
            HPL_DECOMPRESS_DEBUG("File read format failed\n");
            return LOS_NOK;
        }

        /* Get analysis result. */
        if (fgets(analysisLine, LINE_NUM_MAX_LEN, metaFp) == NULL) {
            HPL_DECOMPRESS_DEBUG("File read analysis failed\n");
            return LOS_NOK;
        }

        if (RecordMetadata(decompressor, pathLine, formatLine, lineNum, analysisLine) != LOS_OK) {
            return LOS_NOK;
        }
    }
    return LOS_OK;
}

STATIC uint32_t ReadMetadataFromFile(HplDecompressor *decompressor, uint32_t argc, char *argv[], uint32_t *logIdx)
{
    FILE *metaFp = NULL;
    char *metadataFile = NULL;
    uint32_t ret = LOS_NOK;
    uint32_t i;

    if (strcmp("-m", argv[1]) != 0) {
        HPL_DECOMPRESS_DEBUG("Lack of metadata file\n");
        return LOS_NOK;
    }

    if (OsHplDecompressInit(decompressor) != LOS_OK) {
        return LOS_NOK;
    }
    decompressor->init = TRUE;
    /* 2: The index 2 arguments is metadata */
    i = 2;
    while ((i < argc) && (strcmp("-l", argv[i]) != 0)) {
        metadataFile = argv[i];
        if (metadataFile[0] == '\0') {
            i++;
            continue;
        }
        metaFp = fopen(metadataFile, "r");
        if (metaFp == NULL) {
            HPL_DECOMPRESS_DEBUG("Failed to open metadata file %s\n", metadataFile);
            return LOS_NOK;
        }

        /* Metadata file include note header, jump it. */
        if (JumpNoteHeader(metaFp) != LOS_OK) {
            (VOID)fclose(metaFp);
            return LOS_NOK;
        }

        ret = ReadMetadata(metaFp, decompressor);
        (VOID)fclose(metaFp);
        i++;
    }

    *logIdx = i < argc ? (i + 1) : *logIdx;
    return ret;
}
#endif /* LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR */

STATIC VOID OsHplDecompressFile(uint32_t logFileIndex, uint32_t argc, char *argv[])
{
    uint32_t i = logFileIndex;
    char *logFile = NULL;
    char *buf = NULL;
    struct stat sta;
    FILE *fp = NULL;

    for (; i < argc; i++) {
        logFile = argv[i];

        fp = fopen(logFile, "rb");
        if (fp == NULL) {
            HPL_DECOMPRESS_DEBUG("Failed to open log file\n");
            goto ERR;
        }

        if (stat(logFile, &sta) < 0) {
            HPL_DECOMPRESS_DEBUG("Read stat fail\n");
            goto ERR;
        }

        buf = malloc((size_t)sta.st_size);
        if (buf == NULL) {
            HPL_DECOMPRESS_DEBUG("Malloc buf err, file size %ld\n", sta.st_size);
            goto ERR;
        }

        if (fread(buf, 1, (size_t)sta.st_size, fp) != (size_t)sta.st_size) {
            HPL_DECOMPRESS_DEBUG("Log file read failed\n");
            goto ERR;
        }
        (VOID)OsHplDecompressLog(&g_decompressor, buf, (uint32_t)sta.st_size);
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
        OsHplDecompressLogSort(g_decompressor.sortList, g_decompressor.sortListUsed);
        g_decompressor.sortListUsed = 0;
#endif
ERR:
        if (buf != NULL) {
            free(buf);
            buf= NULL;
        }
        if (fp != NULL) {
            (VOID)fclose(fp);
            fp = NULL;
        }
    }
}

int32_t main(int32_t argc, char *argv[])
{
    uint32_t logFileIndex = 0;

    /* 2: second arguments is 'help'. */
    if (((argc == 2) && (strstr(argv[1], "help"))) || (argc < 2)) {
#ifdef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
        HPL_DECOMPRESS_DEBUG("Usage: decompressor -m metadataFile1 metadataFile2 -l logFile1 logFile2 ...\n");
#else
        HPL_DECOMPRESS_DEBUG("Usage: decompressor logFile1 logFile2 ...\n");
#endif
        return 0;
    }

#ifdef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
    if (ReadMetadataFromFile(&g_decompressor, (uint32_t)argc, argv, &logFileIndex) != 0) {
        return 1;
    }
#else
    logFileIndex = 1;
#endif

    if (logFileIndex != 0) {
        OsHplDecompressFile(logFileIndex, argc, argv);
    }
    return 0;
}
#endif /* LOSCFG_DEBUG_HPL_EMULATION */
