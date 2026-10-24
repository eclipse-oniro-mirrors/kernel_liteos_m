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
#include "los_task_pri.h"
#include "los_memory.h"
#include "los_task.h"
#include "los_hpl.h"
#include "los_hpl_pri.h"
#include "los_hpl_compression_pri.h"
#include "los_hpl_logger_pri.h"
#ifndef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
#include "los_hpl_c_preprocessor_pri.h"
#endif

#ifdef LOSCFG_DEBUG_HPL_EMULATION
#include "los_hpl_emulation_pri.h"
#endif

HplRuntimeLogger g_runtimeLogger = {0};

/* Include an exception handle and an interrupt handle. */
#define HPL_CB_ARRAY_MAX (KERNEL_TSK_LIMIT + LOSCFG_KERNEL_CORE_NUM + 1 + 1)
HplHandle *g_hplCBArray = NULL;

/* Interrupt hpl handle. */
VOID *g_interruptHpl = NULL;
/* Exception hpl handle. */
VOID *g_exceptionHpl = NULL;

VOID OsHplExceptionHandle(VOID)
{
    OsHplExcCompressLog();
#ifdef LOSCFG_DEBUG_HPL_EMULATION
    OsHplExcDecompressLog();
#endif
}

STATIC UINT32 OsHplCreateCompressTask(VOID)
{
    TSK_INIT_PARAM_S taskParam = {0};

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)OsHplCompressMain;
    taskParam.uwStackSize = HPL_COMPRESS_THREAD_STACK_SIZE;
    taskParam.pcName = "LogCompressionTask";
    taskParam.usTaskPrio = (UINT16)HPL_COMPRESS_THREAD_PRIORITY;
    taskParam.uwResved = 0;
    LOS_TASK_PARAM_INIT_ARG(taskParam, &g_runtimeLogger);
    return LOS_TaskCreate(&g_runtimeLogger.compressionTaskId, &taskParam);
}

STATIC UINT32 OsHplCreate(UINT32 bufSize, UINT32 taskId, VOID *handle)
{
    HplHandle   *hpl = (HplHandle *)handle;
    CHAR        *ringBuf = NULL;

    if (hpl->inited == TRUE) {
        return LOS_ERRNO_HPL_HAS_CREATED;
    }

    if (bufSize == 0) {
        return LOS_ERRNO_HPL_STAGING_SIZE_INVALID;
    }
    bufSize = (UINT32)LOS_Align((UINTPTR)bufSize, (UINT32)sizeof(UINT32));
    ringBuf = LOS_MemAlloc(m_aucSysMem0, bufSize);
    if (ringBuf == NULL) {
        return LOS_ERRNO_HPL_NO_MEMORY;
    }

    hpl->hplTaskId = taskId;
    OsHplRingbufInit(&hpl->stageBuf.buffer, ringBuf, bufSize);
    hpl->inited = TRUE;
    return LOS_OK;
}

STATIC VOID OsHplTaskInit(VOID)
{
    UINT32 i;
    for (i = 0; i < LOSCFG_KERNEL_CORE_NUM; i++) {
        g_mainTask[i].hpl = &g_hplCBArray[i];
        LOS_ListTailInsert(&g_runtimeLogger.head, &((HplHandle *)(g_mainTask[i].hpl))->listNode);
    }

    for (i = LOSCFG_KERNEL_CORE_NUM; i < (LOSCFG_KERNEL_CORE_NUM + KERNEL_TSK_LIMIT); i++) {
        g_osTaskCBArray[i - LOSCFG_KERNEL_CORE_NUM].hpl = &g_hplCBArray[i];
        LOS_ListTailInsert(&g_runtimeLogger.head,
                           &((HplHandle *)(g_osTaskCBArray[i - LOSCFG_KERNEL_CORE_NUM].hpl))->listNode);
    }
}

STATIC UINT32 OsHplCreateExcIntHandle(VOID)
{
    g_interruptHpl = &g_hplCBArray[LOSCFG_KERNEL_CORE_NUM + KERNEL_TSK_LIMIT];
    LOS_ListTailInsert(&g_runtimeLogger.head, &((HplHandle *)g_interruptHpl)->listNode);
    if (OsHplCreate(LOS_HPL_RINGBUF_DEFAULT_SIZE, HPL_COMPRESS_INT_EXC_ID, g_interruptHpl) != LOS_OK) {
        return LOS_NOK;
    }

    g_exceptionHpl = &g_hplCBArray[LOSCFG_KERNEL_CORE_NUM + KERNEL_TSK_LIMIT + 1];
    LOS_ListTailInsert(&g_runtimeLogger.head, &((HplHandle *)g_exceptionHpl)->listNode);
    if (OsHplCreate(LOS_HPL_RINGBUF_DEFAULT_SIZE, HPL_COMPRESS_INT_EXC_ID, g_exceptionHpl) != LOS_OK) {
        return LOS_NOK;
    }
    return LOS_OK;
}

LITE_OS_SEC_TEXT_INIT_LEVEL_ONE STATIC UINT32 OsHplInit(VOID)
{
    UINT32 ret = LOS_NOK;
    CHAR *abandonedBuf = NULL;

    if (OsHplLittleEndianCheck() != LOS_OK) {
        return LOS_NOK;
    }
    LOS_ListInit(&g_runtimeLogger.head);

    g_hplCBArray = LOS_MemAlloc(m_aucSysMem0, HPL_CB_ARRAY_MAX * (UINT32)sizeof(HplHandle));
    if (g_hplCBArray == NULL) {
        goto ERR;
    }
    memset(g_hplCBArray, 0, HPL_CB_ARRAY_MAX * (UINT32)sizeof(HplHandle));
    OsHplTaskInit();

    if (LOS_MuxCreate(&g_runtimeLogger.compressMux) != LOS_OK) {
        goto ERR;
    }

    g_runtimeLogger.compressBuf = LOS_MemAlloc(m_aucSysMem0, HPL_COMPRESS_ARGS_BUF_SIZE);
    if (g_runtimeLogger.compressBuf == NULL) {
        goto ERR;
    }

#ifndef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
    LOS_SpinInit(&g_runtimeLogger.lock);
    OsHplCPreprocessInit();
#endif

    abandonedBuf = LOS_MemAlloc(m_aucSysMem0, HPL_ABANDONED_LOG_BUF_SIZE);
    if (abandonedBuf == NULL) {
        goto ERR;
    }
    /* Abandon logs are stored in same buf, so ringbuf init with lock. */
    OsHplRingbufInit(&g_runtimeLogger.abandonedLog, abandonedBuf, HPL_ABANDONED_LOG_BUF_SIZE);

    LOS_SpinInit(&g_runtimeLogger.interruptBufLock);
    ret = LOS_EventInit(&g_runtimeLogger.event);
    if (ret != LOS_OK) {
        goto ERR;
    }

    if (OsHplCreateCompressTask() != LOS_OK) {
        goto ERR;
    }

    if (OsHplCreateExcIntHandle() != LOS_OK) {
        goto ERR;
    }
#ifdef LOSCFG_DEBUG_HPL_EMULATION
    if (OsHplEmulationInit() != LOS_OK) {
        goto ERR;
    }
#endif
    return ret;

ERR:
    return LOS_NOK;
}
LOS_SYS_INIT(OsHplInit, SYS_INIT_LEVEL_KERNEL_ADDITION, SYS_INIT_SYNC_0);

STATIC UINT32 OsHplTaskIdVerify(UINT32 taskId)
{
    LosTaskCB *taskCB = NULL;
    if (OS_TASK_ID_CHECK_INVALID(taskId)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    if (taskCB->taskFlags & OS_TASK_FLAG_SYSTEM) {
        return LOS_ERRNO_TSK_OPERATE_SYSTEM_TASK;
    }

    if (taskCB->taskStatus & OS_TASK_STATUS_UNUSED) {
        return LOS_ERRNO_TSK_NOT_CREATED;
    }
    return LOS_OK;
}

UINT32 LOS_TaskHplPeakSizeGet(UINT32 taskId, UINT32 *peakSize, BOOL *overflow)
{
    HplHandle *hpl = NULL;
    LosTaskCB *taskCB = NULL;
    UINT32 ret;

    if (peakSize == NULL || overflow == NULL) {
        return LOS_ERRNO_HPL_PARAM_INVALID;
    }

    ret = OsHplTaskIdVerify(taskId);
    if (ret != LOS_OK) {
        return ret;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    hpl = (HplHandle *)taskCB->hpl;
    if ((hpl == NULL) || (hpl->inited != TRUE)) {
        return LOS_ERRNO_HPL_NOT_CREATED;
    }

    *peakSize = hpl->stageBuf.buffer.peakSize;
    *overflow = hpl->stageBuf.buffer.overflow;
    return LOS_OK;
}

UINT32 LOS_TaskHplSizeSet(UINT32 taskId, UINT32 bufSize)
{
    LosTaskCB *taskCB = NULL;
    UINT32 ret;

    ret = OsHplTaskIdVerify(taskId);
    if (ret != LOS_OK) {
        return ret;
    }

    taskCB = OS_TCB_FROM_TID(taskId);
    return OsHplCreate(bufSize, taskId, taskCB->hpl);
}

VOID OsHplDelete(HplHandle *handle)
{
    HplHandle *hpl = handle;
    if (hpl->stageBuf.buffer.fifo != NULL) {
        (VOID)LOS_MemFree(m_aucSysMem0, hpl->stageBuf.buffer.fifo);
    }
    hpl->inited = FALSE;
    hpl->preDelete = FALSE;
}

/* Prepare to delete hpl, the final deletion action invoked after compression.  */
VOID OsHplPreDelete(VOID *handle)
{
    HplHandle *hpl = (HplHandle *)handle;
    if (hpl->inited) {
        hpl->preDelete = TRUE;
    }
}

STATIC UINT32 OsHplArgsWriteDone(HplRingbuf *ringbuf, CHAR *data, UINT32 size)
{
    return OsHplRingbufWrite(ringbuf, (const CHAR *)data, size);
}

VOID OsHplRecordAbandonedLog(UINT32 fmtId, UINT64 timestamp)
{
    HplAbandonedLog logInfo;
    logInfo.timestamp = timestamp;
    logInfo.fmtId = fmtId;
    /* Abandoned logs are stored in same buf, so invoke writing interface with lock. */
    (VOID)OsHplRingbufWriteWithLock(&g_runtimeLogger.abandonedLog, (const CHAR *)&logInfo,
                                    (UINT32)sizeof(HplAbandonedLog));
}

STATIC UncompressedEntry* OsHplCreateUncompressEntry(CHAR *buf, UINT32 fmtId)
{
    UncompressedEntry *ue = NULL;
    ue = (UncompressedEntry *)buf;
    ue->entryMagic = HPL_UNCOMPRESSED_ENTRY_MAGIC;
    ue->fmtId = fmtId;
    /* Use LOS_SysCycleGet instead of LOS_CurrNanosec to improve performance. */
    ue->timestamp = LOS_SysCycleGet();
    ue->entrySize = (UINT32)sizeof(UncompressedEntry);
    return ue;
}

STATIC HplHandle* OsHplGetHplHandle(VOID)
{
    HplHandle *hpl = NULL;
    UINT32 ret;
    if (OS_EXC_ACTIVE) {
        hpl = (HplHandle *)g_exceptionHpl;
    } else if (OS_INT_ACTIVE) {
        hpl = (HplHandle *)g_interruptHpl;
    } else {
        hpl = OsCurrTaskHplGet();
        if (!hpl->inited) {
            ret = OsHplCreate(LOS_HPL_RINGBUF_DEFAULT_SIZE, OsCurrTaskGet()->taskId, hpl);
            if (ret != LOS_OK) {
                HPL_DEBUG_PRINT("hpl create failed task %u, task name %s, err %x\n", LOS_CurTaskIDGet(),
                                OsCurrTaskGet()->taskName, ret);
                return NULL;
            }
        }
        if (hpl->inited && hpl->preDelete) {
            /* if delete but not released, actively release */
            OsHplCompressLog();
            ret = OsHplCreate(LOS_HPL_RINGBUF_DEFAULT_SIZE, OsCurrTaskGet()->taskId, hpl);
            if (ret != LOS_OK) {
                HPL_DEBUG_PRINT("hpl reclaim failed task %u, task name %s, err %x\n", LOS_CurTaskIDGet(),
                                OsCurrTaskGet()->taskName, ret);
                return NULL;
            }
        }
    }
    return hpl;
}

/* Storage the log argument and format string in staging buffer.
 * ·Each task has an independent staging buf to store task log.
 * ·All interrupts store log into the same staging buf.
 * ·Exception store log into the task staging buffer which cause exception. */
#ifdef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
VOID OsHplLogger(UINT32 severity, va_list arg_ptr, UINT32 fmtId, UINT64 analysis,
                 UINT32 customArgsNum, ...)
#else
VOID OsHplLogger(UINT32 *fmtId, UINT64 *analysis, OsHplStaticInfo *fileInfo,
                 UINT32 severity, const CHAR *format, ...)
#endif
{
    HplHandle           *hpl = NULL;
    UINT32              argsNum, i, writeSize, ret;
    UncompressedEntry   *ue = NULL;
    CHAR                buf[HPL_ARGUMENTS_MAX_SIZE];

#ifdef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
    va_list             custom_arg_ptr;
    UINT64 logAnalysis = analysis;
    UINT32 logFmtId = fmtId;
#else
    if ((format == NULL) || ((severity & OsHplGetLogLevel()) != severity) ||
        ((OsHplAssistant(fmtId, analysis, fileInfo, format) != LOS_OK))) {
        return;
    }
    UINT64 logAnalysis = *analysis;
    UINT32 logFmtId = *fmtId;
    va_list arg_ptr;
    UINT32 customArgsNum = 0;
#endif

    if (logAnalysis == LOS_HPL_INVALID_ANALYSIS) {
        return;
    }
    hpl = OsHplGetHplHandle();
    if (hpl == NULL) {
        return;
    }

    ue = OsHplCreateUncompressEntry(buf, logFmtId);
    writeSize = (UINT32)sizeof(UncompressedEntry);

#ifdef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
    va_start(custom_arg_ptr, customArgsNum);
    for (i = 0; i < customArgsNum; i++) {
        ret = OsHplRecordArgs(HPL_NEXT_ARGS_TYPE(logAnalysis, i), ue, buf, &writeSize, &custom_arg_ptr);
        if (ret != LOS_OK) {
            va_end(custom_arg_ptr);
            HPL_DEBUG_PRINT("OsHplRecordArgs fmtid %#x err\n", logFmtId);
            goto OUT;
        }
    }
    va_end(custom_arg_ptr);
#else
    va_start(arg_ptr, format);
#endif
    argsNum = HPL_GET_ARGS_NUM(logAnalysis);
    for (i = customArgsNum; i < argsNum; i++) {
        ret = OsHplRecordArgs(HPL_NEXT_ARGS_TYPE(logAnalysis, i), ue, buf, &writeSize, &arg_ptr);
        if (ret != LOS_OK) {
            HPL_DEBUG_PRINT("OsHplRecordArgs fmtid %#x err\n", logFmtId);
            goto OUT;
        }
    }
    /* All interrupt log arguments are stored in one staging buf. */
#if defined(LOSCFG_ARCH_INTERRUPT_PREEMPTION) || defined(LOSCFG_KERNEL_SMP)
    UINT32 intSave = 0;
    if (OS_INT_ACTIVE) {
        LOS_SpinLockSave(&g_runtimeLogger.interruptBufLock, &intSave);
    }
#endif
    ret = OsHplArgsWriteDone(&hpl->stageBuf.buffer, buf, writeSize);
    if (ret != writeSize) {
        OsHplRecordAbandonedLog(logFmtId, ue->timestamp);
    }
#if defined(LOSCFG_ARCH_INTERRUPT_PREEMPTION) || defined(LOSCFG_KERNEL_SMP)
    if (OS_INT_ACTIVE) {
        LOS_SpinUnlockRestore(&g_runtimeLogger.interruptBufLock, intSave);
    }
#endif
OUT:
#ifndef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
    va_end(arg_ptr);
#endif
    return;
}
