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

#include "los_init.h"
#include "los_hpl.h"
#include "los_hpl_compression_pri.h"
#ifdef LOSCFG_DEBUG_HPL_EMULATION
#include "los_hpl_emulation_pri.h"
#endif
#include "los_hpl_logger_pri.h"
HplRuntimeLogger g_runtimeLogger = {0};

VOID OsHplExceptionHandle(VOID)
{
#ifdef LOSCFG_DEBUG_HPL_EMULATION
    OsHplExcDecompressLog();
#endif
}

STATIC UINT32 OsHplInit(VOID)
{
    if (OsHplLittleEndianCheck() != LOS_OK) {
        return LOS_NOK;
    }

#ifdef LOSCFG_DEBUG_HPL_EMULATION
    if (OsHplEmulationInit() != LOS_OK) {
        return LOS_NOK;
    }
    if (LOS_EventInit(&g_runtimeLogger.event) != LOS_OK) {
        return LOS_NOK;
    }
#endif

    return LOS_OK;
}
LOS_SYS_INIT(OsHplInit, SYS_INIT_LEVEL_KERNEL_ADDITION, SYS_INIT_SYNC_0);

STATIC VOID* OsHplCreateCompressEntry(CHAR *buf, UINT32 fmtId, UINT32 *writeSize)
{
    HplCompressedArgsEntry *entry = (HplCompressedArgsEntry *)buf;
    entry->argsEntryMagic = HPL_ARGS_ENTRY_MAGIC;
    entry->entryType = ENTRY_TYPE_ARGS_ENTRY;
    entry->additionalTimeStampBytes = 0;
    entry->totalEntryLength = (UINT32)sizeof(HplCompressedArgsEntry);
    entry->formatId = fmtId;
    entry->protocolVersion = HPL_CHECKPOINT_VERSION;
    *writeSize = (UINT32)sizeof(HplCompressedArgsEntry);
    return buf;
}

/* Storage the log argument with lock. */
VOID OsHplLogger(UINT32 severity, va_list arg_ptr, UINT32 fmtId, UINT64 analysis,
                 UINT32 customArgsNum, ...)
{
    UINT32  argsNum, i, writeSize, ret;
    va_list custom_arg_ptr;
    VOID    *ue = NULL;
    CHAR    buf[HPL_ARGUMENTS_MAX_SIZE];

    UINT64 logAnalysis = analysis;
    UINT32 logFmtId = fmtId;

    if (logAnalysis == LOS_HPL_INVALID_ANALYSIS) {
        return;
    }

    ue = OsHplCreateCompressEntry(buf, logFmtId, &writeSize);
    va_start(custom_arg_ptr, customArgsNum);
    for (i = 0; i < customArgsNum; i++) {
        ret = OsHplRecordArgs(HPL_NEXT_ARGS_TYPE(logAnalysis, i), ue, buf, &writeSize, &custom_arg_ptr);
        if (ret != LOS_OK) {
            HPL_DEBUG_PRINT("OsHplRecordArgs fmtid %#x err\n", logFmtId);
            g_runtimeLogger.excLogFlag[HPL_ILLEGAL_LOG_IDX] = TRUE;
            va_end(custom_arg_ptr);
            goto OUT;
        }
    }
    va_end(custom_arg_ptr);
    argsNum = HPL_GET_ARGS_NUM(logAnalysis);
    for (i = customArgsNum; i < argsNum; i++) {
        ret = OsHplRecordArgs(HPL_NEXT_ARGS_TYPE(logAnalysis, i), ue, buf, &writeSize, &arg_ptr);
        if (ret != LOS_OK) {
            g_runtimeLogger.excLogFlag[HPL_ILLEGAL_LOG_IDX] = TRUE;
            HPL_DEBUG_PRINT("OsHplRecordArgs fmtid %#x err\n", logFmtId);
            goto OUT;
        }
    }
    ret = OsHplMetadataRecord(&g_runtimeLogger, (const UINT8 *)buf, writeSize);
    if (ret != LOS_OK) {
        g_runtimeLogger.excLogFlag[HPL_ABANDONED_LOG_IDX] = TRUE;
        HPL_DEBUG_PRINT("logger fmtid %#x err\n", logFmtId);
        goto OUT;
    }
OUT:
    /* Exception flag stored in global variables because the following function may return failed,
     * therefore the exception status preocessed with next log. */
    if (g_runtimeLogger.excLogFlag[HPL_ILLEGAL_LOG_IDX]) {
        OsHplOutputExcInfo(HPL_ILLEGAL_LOG_IDX);
    }
    if (g_runtimeLogger.excLogFlag[HPL_ABANDONED_LOG_IDX]) {
        OsHplOutputExcInfo(HPL_ABANDONED_LOG_IDX);
    }
}
