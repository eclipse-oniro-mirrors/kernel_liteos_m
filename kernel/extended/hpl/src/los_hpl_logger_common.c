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
#include "los_memory.h"
#ifdef LOSCFG_DEBUG_HPL_EMULATION
#include "los_hpl_emulation_pri.h"
#endif

/* High performance log only support little-endian. */
UINT32 OsHplLittleEndianCheck(VOID)
{
    INT32 i = 1;
    UINT8 c = (*(UINT8*)&i);

    if (c != 0) {
        return LOS_OK;
    } else {
        return LOS_NOK;
    }
}

UINT32 LOS_HplSetLogLevel(UINT32 level)
{
    g_runtimeLogger.loglevel = level;
    return LOS_OK;
}

UINT32 OsHplGetLogLevel(VOID)
{
    return g_runtimeLogger.loglevel;
}

UINT32 LOS_HplSetLogRecordHook(ARGS_RECORD_FUNC recordFunc)
{
    if (recordFunc == NULL) {
        return LOS_ERRNO_HPL_PARAM_INVALID;
    }
    g_runtimeLogger.recordHook = recordFunc;
    return LOS_OK;
}

UINT32 LOS_HplRecordCustomString(CHAR *strBuf, UINT32 bufSize)
{
    UINT8 customString[HPL_CUSTOM_BUF_SIZE];
    HplCustomStringHeader *stringHeader = NULL;
    UINT8 *stringBuf = NULL;
    UINT32 stringBufLen;
    errno_t err;
    UINT32 ret = LOS_OK;

    if ((strBuf == NULL) || (bufSize == 0)) {
        return LOS_ERRNO_HPL_PARAM_INVALID;
    }

    if (bufSize > (HPL_CUSTOM_BUF_SIZE - (UINT32)sizeof(HplCustomStringHeader))) {
        stringBuf = LOS_MemAlloc(m_aucSysMem0, bufSize + (UINT32)sizeof(HplCustomStringHeader));
        if (stringBuf == NULL) {
            HPL_DEBUG_PRINT("hpl malloc string buff failed\n");
            return LOS_NOK;
        }
        stringHeader = (HplCustomStringHeader *)stringBuf;
        stringBufLen = bufSize + (UINT32)sizeof(HplCustomStringHeader);
    } else {
        stringHeader = (HplCustomStringHeader *)customString;
        stringBufLen = HPL_CUSTOM_BUF_SIZE;
    }
    stringHeader->customMagic = HPL_USER_CUSTOM_STRING_MAGIC;
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
    stringHeader->timestamp = LOS_SysCycleGet();
#endif
    stringHeader->dataLength = bufSize;

    err = memcpy_s(stringHeader->data, stringBufLen - (UINT32)sizeof(HplCustomStringHeader), strBuf, bufSize);
    if (err != EOK) {
        g_runtimeLogger.excLogFlag[HPL_ILLEGAL_LOG_IDX] = TRUE;
        HPL_DEBUG_PRINT("Custom string size overflow\n");
        ret = LOS_ERRNO_HPL_CUSTOM_SIZE_OVERFLOW;
        goto OUT;
    }

    if (OsHplMetadataRecord(&g_runtimeLogger, (UINT8 *)stringHeader,
        bufSize + (UINT32)sizeof(HplCustomStringHeader)) != LOS_OK) {
        g_runtimeLogger.excLogFlag[HPL_ABANDONED_LOG_IDX] = TRUE;
        ret = LOS_NOK;
    }
OUT:
    if (stringBuf != NULL) {
        (void)LOS_MemFree(m_aucSysMem0, stringBuf);
    }
    return ret;
}

UINT32 OsHplMetadataRecord(HplRuntimeLogger *logger, const UINT8 *buf, UINT32 size)
{
#ifdef LOSCFG_DEBUG_HPL_EMULATION
    return OsHplEmulationWrite(logger, buf, size);
#else
    if (logger->recordHook == NULL) {
        return LOS_NOK;
    }
    return logger->recordHook(buf, size);
#endif
}

#define HPL_EXC_LOG_BUF_LEN 70
VOID OsHplOutputExcInfo(UINT32 excLogType)
{
    UINT8 customString[HPL_EXC_LOG_BUF_LEN];
    HplCustomStringHeader *stringHeader = (HplCustomStringHeader *)customString;

    CHAR *excLogWarning[] = {"[hpl_online] Illegal log! Check string size.\n",
                             "[hpl_online] Abandoned log! Reduce log.\n"};
    stringHeader->customMagic = HPL_USER_CUSTOM_STRING_MAGIC;
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
    stringHeader->timestamp = LOS_SysCycleGet();
#endif
    stringHeader->dataLength = (UINT32)strlen(excLogWarning[excLogType]);
    if (memcpy_s(stringHeader->data, HPL_EXC_LOG_BUF_LEN - (UINT32)sizeof(HplCustomStringHeader),
        excLogWarning[excLogType], stringHeader->dataLength) != EOK) {
        return;
    }
    if (OsHplMetadataRecord(&g_runtimeLogger, (UINT8 *)stringHeader, stringHeader->dataLength +
        (UINT32)sizeof(HplCustomStringHeader)) != LOS_OK) {
        return;
    }
    g_runtimeLogger.excLogFlag[excLogType] = FALSE;
}

STATIC INLINE UINT32 OsHplRecord(CHAR *buf, UINT32 *used, const VOID *data, UINT32 size)
{
    UINT32 remain = HPL_ARGUMENTS_MAX_SIZE - *used;
    if (memcpy_s(&buf[*used], remain, data, size) != EOK) {
        return LOS_NOK;
    }
    *used += size;
    return LOS_OK;
}

STATIC INLINE UINT32 OsHplRecordString(UINT32 *bytes, CHAR *buf, UINT32 *used, va_list *arg_ptr)
{
    UINT32 ret;
    va_list ptr = *arg_ptr;
    CHAR *string = va_arg(ptr, CHAR *);
    if (string == NULL) {
        string = "";
    }
    ret = OsHplRecord(buf, used, string, (UINT32)strlen(string) + 1);
    if (ret != LOS_OK) {
        return ret;
    }
    *bytes = (UINT32)strlen(string) + 1;
    *arg_ptr = ptr;
    return LOS_OK;
}

STATIC INLINE UINT32 OsHplRecordDoubleBytes(UINT32 *bytes, CHAR *buf, UINT32 *used, va_list *arg_ptr)
{
    UINT32 ret;
    va_list ptr = *arg_ptr;
    DOUBLE floatType = va_arg(ptr, DOUBLE);

    ret = OsHplRecord(buf, used, &floatType, (UINT32)sizeof(DOUBLE));
    if (ret != LOS_OK) {
        return ret;
    }
    *bytes = (UINT32)sizeof(DOUBLE);
    *arg_ptr = ptr;
    return LOS_OK;
}

STATIC INLINE UINT32 OsHplRecord8Bytes(UINT32 *bytes, CHAR *buf, UINT32 *used, va_list *arg_ptr)
{
    UINT32 ret;
    va_list ptr = *arg_ptr;
    UINT64 data64 = va_arg(ptr, UINT64);
    ret = OsHplRecord(buf, used, &data64, (UINT32)sizeof(UINT64));
    if (ret != LOS_OK) {
        return ret;
    }
    *bytes = (UINT32)sizeof(UINT64);
    *arg_ptr = ptr;
    return LOS_OK;
}

STATIC INLINE UINT32 OsHplRecord4Bytes(UINT32 *bytes, CHAR *buf, UINT32 *used, va_list *arg_ptr)
{
    UINT32 ret;
    va_list ptr = *arg_ptr;
    UINT32 data32 = va_arg(ptr, UINT32);
    ret = OsHplRecord(buf, used, &data32, (UINT32)sizeof(UINT32));
    if (ret != LOS_OK) {
        return ret;
    }
    *bytes = (UINT32)sizeof(UINT32);
    *arg_ptr = ptr;
    return LOS_OK;
}

UINT32 OsHplRecordArgs(UINT32 argsType, VOID *ue, CHAR *buf, UINT32 *used, va_list *arg_ptr)
{
    UINT32 ret;
    UINT32 bytes = 0;
    /* Use 'if' statement rather than function pointer array,
        * because function pointer trigger invoking which led to performance deterioration 10%. */
    if (argsType == ARGS_TYPE_4_BYTES) {
        ret = OsHplRecord4Bytes(&bytes, buf, used, arg_ptr);
    } else if (argsType == ARGS_TYPE_8_BYTES) {
        ret = OsHplRecord8Bytes(&bytes, buf, used, arg_ptr);
    } else if (argsType == ARGS_TYPE_STRING) {
        ret = OsHplRecordString(&bytes, buf, used, arg_ptr);
    } else if (argsType == ARGS_TYPE_DOUBLE) {
        ret = OsHplRecordDoubleBytes(&bytes, buf, used, arg_ptr);
    } else {
        HPL_DEBUG_PRINT("Invalid Arguments type %u\n", argsType);
        ret = LOS_NOK;
    }
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
    ((UncompressedEntry *)ue)->entrySize += bytes;
#else
    ((HplCompressedArgsEntry *)ue)->totalEntryLength += bytes;
#endif
    return ret;
}

#ifdef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
/*
 * This function is used for adding custom log prefix. User implement by redefined OsHplLoggerHook and HP_LOG.
 * eg.
 * 1. #defind CUSTOM_LOG(loglevel, fmt, args...) HP_LOG(loglevel, "[%5u.%06u][cpu%u]"fmt, args)
 * 2. Redefined OsHplLoggerHook and invoke OsHplLogger(line, severity, arg_ptr, fmtId, analysis, 3, 1, 1, 1)
 * Note: '3' is the number of custom arguments, followed by the parameters, which is corresponding with custom
 *       format string prefix.
 */
__attribute__((weak)) VOID OsHplLoggerHook(UINT32 severity, UINT32 fmtId, UINT64 analysis, ...)
{
    if ((severity & OsHplGetLogLevel()) != severity) {
        return;
    }

    va_list arg_ptr;
    va_start(arg_ptr, analysis);
    OsHplLogger(severity, arg_ptr, fmtId, analysis, 0);
    va_end(arg_ptr);
}
#endif
