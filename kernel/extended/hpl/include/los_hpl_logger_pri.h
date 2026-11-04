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

#ifndef _LOS_HPL_LOGGER_PRI_H
#define _LOS_HPL_LOGGER_PRI_H

#include "los_typedef.h"
#include "los_list.h"
#include "los_spinlock.h"
#include "los_event.h"
#include "los_exc.h"
#ifdef LOSCFG_DEBUG_HPL_EMULATION
#include "los_ringbuf.h"
#endif
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
#include "los_hpl_ringbuf_pri.h"
#endif
#include "los_hpl_common_pri.h"
#include "los_mux.h"
#include "los_task_pri.h"
#include "los_hpl.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Indicates an invalid log which arguments size overflow or arguments num more than 28. */
#define LOS_HPL_INVALID_ANALYSIS        0xFEFEFEFE

#define HPL_UNCOMPRESSED_ENTRY_MAGIC    0xFCFCFCFC
#define HPL_UNCOMPRESSED_FMT_MAGIC      0xFAFAFAFA

#define HPL_COMPRESS_ARGS_BUF_SIZE      2048
#define HPL_LOGGER_FMT_BUF_SIZE         2048

#if defined(LOSCFG_KERNEL_HPL_PARAM_MAX_SIZE) && LOSCFG_KERNEL_HPL_PARAM_MAX_SIZE <= 0
    #pragma message(PRINT_MACRO(LOSCFG_KERNEL_HPL_PARAM_MAX_SIZE))
    #error "Invalid parameters buffer size!"
#endif
#define HPL_CUSTOM_BUF_SIZE             (LOSCFG_KERNEL_HPL_PARAM_MAX_SIZE + (UINT32)sizeof(HplCustomStringHeader))

#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
#define HPL_ARGUMENTS_MAX_SIZE          (LOSCFG_KERNEL_HPL_PARAM_MAX_SIZE + (UINT32)sizeof(UncompressedEntry))
#else
#define HPL_ARGUMENTS_MAX_SIZE          (LOSCFG_KERNEL_HPL_PARAM_MAX_SIZE + (UINT32)sizeof(HplCompressedArgsEntry))
#endif

#define HPL_ABANDONED_LOG_BUF_SIZE      2048

#define HPL_TRIGGER_EVENT               (0x1 << 0)
#define HPL_COMPRESS_MAIN_EXIT          (0x1 << 1)
#define HPL_COMPRESS_DONE               (0x1 << 2)

#define HPL_COMPRESS_ALL_TASK           0xFFFFFFFF
#define HPL_COMPRESS_INT_EXC_ID         0xEFEFEFEF

#ifndef __LP64__
#define HPL_COMPRESS_THREAD_STACK_SIZE                      2560
#else
#define HPL_COMPRESS_THREAD_STACK_SIZE                      4096
#endif
#define HPL_COMPRESS_THREAD_PRIORITY                        LOSCFG_KERNEL_HPL_COMPRESS_TASK_PRIORITY

#define HPL_DECOMPRESS_THREAD_STACK_SIZE                    (HPL_COMPRESS_THREAD_STACK_SIZE * 4)

#ifdef OS_KERNEL_HPL_DEBUG
#define HPL_DEBUG_PRINT(fmt, ...)      dprintf("[hpl_online] "fmt, ##__VA_ARGS__)
#else
#define HPL_DEBUG_PRINT(fmt, ...)
#endif

enum {
    HPL_ILLEGAL_LOG_IDX = 0,
    HPL_ABANDONED_LOG_IDX,
    HPL_EXC_LOG_MAX_IDX,
};

/* Force to 4 bytes align to avoid compiler optimize to 8 bytes align. */
#pragma pack(push, 4)
typedef struct {
    UINT32      entryMagic;
    UINT32      fmtId;
    UINT32      entrySize;   /* Number of bytes for uncompressed log arguments and this header. */
    UINT64      timestamp;
    CHAR        argData[0];  /* Uncompressed arguments that required by the original format string. */
} UncompressedEntry;
#pragma pack(pop)

#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
typedef struct {
    HplRingbuf      buffer; /* Each task has individual buffer for uncompressed arguments. */
} HplStageBuffer;

typedef struct {
    LOS_DL_LIST     listNode;
    HplStageBuffer  stageBuf;
    UINT32          hplTaskId;
    BOOL            inited;
    BOOL            preDelete;
} HplHandle;
#endif

typedef struct {
    UINT32           loglevel;
    ARGS_RECORD_FUNC recordHook;
    BOOL             excLogFlag[HPL_EXC_LOG_MAX_IDX];
#if defined(LOSCFG_DEBUG_HPL_EMULATION) || defined(LOSCFG_KERNEL_HPL_TASK_LOCK_FREE)
    EVENT_CB_S       event;
#endif
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
    LOS_DL_LIST      head;
    SPIN_LOCK_S      interruptBufLock;
    HplRingbuf       abandonedLog;

    /* Indicates whether checkpoint is written. */
    BOOL            checkpointFlag;
    UINT32          compressionTaskId;
    UINT8           *compressBuf;
    UINT32          compressMux;
#endif
#ifndef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
    SPIN_LOCK_S     lock;
    UINT32          maxFmtId;
    HplRingbuf      fmtRingbuf;
    UINT32          fmtRecordCnt;
#endif
#ifdef LOSCFG_DEBUG_HPL_EMULATION
    UINT32          decompressionTaskId;
    Ringbuf         shareMem;
    CHAR            *shareBuf;
    UINT32          shareBufSize;
#endif
} HplRuntimeLogger;

extern HplRuntimeLogger g_runtimeLogger;

#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
STATIC INLINE HplHandle *OsCurrTaskHplGet(VOID)
{
    return (HplHandle *)(OsCurrTaskGet()->hpl);
}
extern VOID OsHplDelete(HplHandle *handle);
#endif

#ifdef LOSCFG_OBSOLETE_API
extern VOID *OsHplCompressMain(UINTPTR, UINTPTR, UINTPTR, UINTPTR);
#else
extern VOID *OsHplCompressMain(VOID *priv);
#endif

extern VOID OsHplRecordAbandonedLog(UINT32 fmtId, UINT64 timestamp);
extern UINT32 OsHplRecordCustomLog(HplRuntimeLogger *logger, CHAR *strBuf, UINT32 bufSize);
extern UINT32 OsHplLittleEndianCheck(VOID);
extern UINT32 OsHplRecordArgs(UINT32 argsType, VOID *ue, CHAR *buf, UINT32 *used, va_list *arg_ptr);
extern UINT32 OsHplGetSerialIdx(HplRuntimeLogger *logger);
extern VOID OsHplOutputExcInfo(UINT32 excLogType);
extern UINT32 OsHplMetadataRecord(HplRuntimeLogger *logger, const UINT8 *buf, UINT32 size);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LOS_HPL_LOGGER_PRI_H */
