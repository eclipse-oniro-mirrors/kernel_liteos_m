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

#include "los_task.h"
#include "los_memory.h"
#include "los_ringbuf.h"
#include "los_hpl_pri.h"
#include "los_hpl_emulation_pri.h"

VOID OsHplEmulationDeinit(VOID)
{
    if (g_runtimeLogger.decompressionTaskId != 0) {
        (VOID)LOS_TaskDelete(g_runtimeLogger.decompressionTaskId);
    }

    if (g_runtimeLogger.shareBuf != NULL) {
        (VOID)LOS_MemFree(m_aucSysMem0, g_runtimeLogger.shareBuf);
    }
}

UINT32 OsHplEmulationInit(VOID)
{
    UINT32 ret = LOS_NOK;
    TSK_INIT_PARAM_S taskParam = {0};
    VOID *shareMem = NULL;
    UINT32 shareMemSize = HPL_EMULATION_SHARE_SIZE;

    taskParam.uwStackSize = HPL_DECOMPRESS_THREAD_STACK_SIZE;
    taskParam.pcName = "LogDecompressionTask";
    taskParam.usTaskPrio = (UINT16)HPL_DECOMPRESS_THREAD_PRIORITY;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)OsHplDecompressMain;
    taskParam.uwResved = 0;
    LOS_TASK_PARAM_INIT_ARG(taskParam, &g_runtimeLogger);
    ret = LOS_TaskCreate(&g_runtimeLogger.decompressionTaskId, &taskParam);
    if (ret != LOS_OK) {
        return ret;
    }

    shareMem = LOS_MemAlloc(m_aucSysMem0, shareMemSize);
    if (shareMem == NULL) {
        ret = LOS_NOK;
        goto ERR;
    }
    (VOID)LOS_RingbufInit(&g_runtimeLogger.shareMem, shareMem, shareMemSize, RBUF_NORMAL);
    g_runtimeLogger.shareBuf = shareMem;
    g_runtimeLogger.shareBufSize = shareMemSize;
    return LOS_OK;
ERR:
    (VOID)LOS_TaskDelete(g_runtimeLogger.decompressionTaskId);
    g_runtimeLogger.decompressionTaskId = 0;
    return ret;
}

UINT32 OsHplEmulationWrite(HplRuntimeLogger *logger, const UINT8 *buf, UINT32 size)
{
    UINT32 wrSize = 0;
    while ((logger->shareBufSize - LOS_RingbufUsedSize(&logger->shareMem)) < size) {
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
        if (OS_EXC_ACTIVE) {
            return LOS_NOK;
        }
#endif
        (VOID)LOS_EventWrite(&logger->event, HPL_COMPRESS_DONE);
    }
    wrSize = LOS_RingbufWrite(&logger->shareMem, (const CHAR *)buf, size);
    if (wrSize != size) {
        HPL_DEBUG_PRINT("Record metadate failed\n");
        return LOS_NOK;
    }
    return LOS_OK;
}

#if LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
STATIC UINT32 ReadMetadataFromCode(HplDecompressor *decompressor)
{
    HplMetadataHeader *head = (HplMetadataHeader *)__hpl_metadata_start;
    HplMetadataInfo *metadata = NULL;
    HplDecompressFormat *decompressFormat = NULL;
    UINT32 pathStringBytes;
    UINT32 formatStringBytes;
    UINT32 functionNameBytes;
    UINT32 totalStringSize;
    UINT32 formatNum;
    UINT32 ret;
    UINT32 i;
    errno_t err;

    if ((head->magic != HPL_CHECKPOINT_MAGIC) || (head->version != HPL_CHECKPOINT_VERSION)) {
        HPL_DECOMPRESS_DEBUG("Compressor and decompressor incompatible, log version %x\n", head->version);
        return LOS_NOK;
    }

    if (OsHplDecompressInit(decompressor) != LOS_OK) {
        return LOS_NOK;
    }
    decompressor->init = TRUE;
    metadata = (HplMetadataInfo *)(__hpl_metadata_start + sizeof(HplMetadataHeader));

    formatNum = head->formatNum;
    for (i = 0; i < formatNum; i++) {
        pathStringBytes = strlen(metadata[i].filename) + 1;
        formatStringBytes = strlen(metadata[i].formatString) + 1;
        functionNameBytes = 0;
        totalStringSize = pathStringBytes + formatStringBytes + functionNameBytes;
        decompressFormat = HPL_DECOMPRESS_MALLOC(totalStringSize + (UINT32)sizeof(HplDecompressFormat));
        if (decompressFormat == NULL) {
            HPL_DECOMPRESS_DEBUG("Alloc memory failed\n");
            ret = LOS_NOK;
            break;
        }
        decompressFormat->filenameBytes = pathStringBytes;
        decompressFormat->formatStringBytes = formatStringBytes;
        decompressFormat->funcBytes = functionNameBytes;
        decompressFormat->linenum = metadata[i].linenum;
        decompressFormat->fmtAnalysis = metadata[i].analysis;
        decompressFormat->fmtId = i;

        err = memcpy_s(decompressFormat->data, totalStringSize, metadata[i].filename, pathStringBytes);
        if (err != EOK) {
            return LOS_NOK;
        }
        err = memcpy_s(decompressFormat->data + pathStringBytes, totalStringSize, metadata[i].formatString,
                       formatStringBytes);
        if (err != EOK) {
            return LOS_NOK;
        }
        ret = OsHplSaveFormat(decompressor, HPL_SYSTEM_MODULE_ID, decompressFormat);
        if (ret != LOS_OK) {
            break;
        }
    }
    return ret;
}
#endif

#ifdef LOSCFG_OBSOLETE_API
VOID *OsHplDecompressMain(UINTPTR priv,  UINTPTR param1,  UINTPTR param2,  UINTPTR param3)
#else
VOID *OsHplDecompressMain(VOID *priv)
#endif
{
    HplRuntimeLogger *logger = (HplRuntimeLogger *)priv;
    CHAR *buf = NULL;

#ifdef LOSCFG_OBSOLETE_API
    (VOID)param1;
    (VOID)param2;
    (VOID)param3;
#endif
    buf = (CHAR *)HPL_DECOMPRESS_MALLOC(logger->shareBufSize);
    if (buf == NULL) {
        HPL_DECOMPRESS_DEBUG("malloc failed\n");
        return NULL;
    }
    g_decompressor.logger = (HplRuntimeLogger *)priv;
    g_decompressor.decompressBuf = buf;

#if LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
    if (ReadMetadataFromCode(&g_decompressor) != LOS_OK) {
        HPL_DECOMPRESS_DEBUG("hpl emulation and preprocessor init failed\n");
        return NULL;
    }
#endif

    while (1) {
        (VOID)LOS_EventRead(&logger->event, HPL_COMPRESS_DONE, LOS_WAITMODE_OR | LOS_WAITMODE_CLR,
                            LOS_MS2Tick(HPL_TASK_SLEEP_MS));
        if (OsHplDecompressLog(&g_decompressor, buf, LOS_RingbufRead(&logger->shareMem, buf, logger->shareBufSize)) !=
            LOS_OK) {
            return NULL;
        }
    }
    return NULL;
}

VOID OsHplExcDecompressLog(VOID)
{
    HplRuntimeLogger *logger = g_decompressor.logger;
    (VOID)OsHplDecompressLog(&g_decompressor, g_decompressor.decompressBuf,
                             LOS_RingbufRead(&logger->shareMem, g_decompressor.decompressBuf, logger->shareBufSize));
}
