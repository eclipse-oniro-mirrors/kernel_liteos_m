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

#ifndef _LOS_HPL_COMPRESSION_PRI_H
#define _LOS_HPL_COMPRESSION_PRI_H

#include "los_typedef.h"
#include "los_hpl_common_pri.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Indicates an compressed log file which written at the beginning of log file. */
#define HPL_CHECKPOINT_MAGIC            0xDEADBEEF
/* The version of compression and decompression protocol. */
#define HPL_CHECKPOINT_VERSION          0x1

/* Indicates a format info. */
#define HPL_FMT_ENTRY_MAGIC             0x4F3E89AD

/* Indicates a fragment of arguments, including one or more argmuents. */
#define HPL_ARGS_FRAGMENT_MAGIC         0x4F3E89AE
/* Indicates a arguments info. */
#define HPL_ARGS_ENTRY_MAGIC            0x4F3E89AF

/* Indicates an abandoned log header. */
#define HPL_ABANDONED_LOG_ENTRY_MAGIC   0xDEADDEAD
/* Every startup time, write CheckPoint to indicate a new log file. */
typedef struct {
    UINT32      magic;
    UINT32      version;
} HplCheckPoint;

/* Save the static info in the following format:
 * HplCompressedFormatEntry--analysis--filename--function--formatstring */
typedef struct {
    UINT32 fmtEntryMagic;

    /* Byte representation of an HplEntryType to indicate the start of an format entry. */
    UINT32 entryType : 4;

    /* Format string analysis result, arguments number and type. */
    UINT32 fmtAnalysisBytes : 8;

    /* File line number in which the original log invocation appeared. */
    UINT32 linenum : 20;

    /* Length of the filename that is associated with this log invocation and follows after this structure. */
    UINT32 filenameLength : 12;

    /* Length of the function string that is associated with this log invocation and comes after filename. */
    UINT32 funcStringLength : 8;

    /* Length of the format string that is associated with this log invocation and comes after function string. */
    UINT32 formatStringLength : 12;
} HplCompressedFormatEntry;

typedef struct {
    UINT32       argsFragmentMagic;

    /* Byte representation of an HplEntryType to indicate the start of an arguments fragment. */
    UINT32       fragmentType : 4;

    /* Number of bytes for this arguments fragment, include this header. */
    UINT32       argsDataBytes : 28;

    /* Total number of argemnets log in this arguments fragment. */
    UINT32       totalArgsEntrys;
} HplArgsFragment;

/* Save the arguments in the following format: "|---HplCompressedArgsInfo--TimeStamp--argData---|" */
typedef struct {
    UINT32       argsEntryMagic;

     /* Byte representation of an HplEntryType to indicate the start of an arguments entry. */
    UINT32       entryType : 4;

    /* Indicates the bytes number of timestamp,
     * the args timestamp calculated from basic timestamp and additional timestamp value. */
    UINT32       additionalTimeStampBytes : 4;

    /* Version of compress protocol. */
    UINT32       protocolVersion : 8;

    /* Length of compressed arguments, timestamp and this header. */
    UINT32       totalEntryLength : 16;

    /* The format Id of this arguments. */
    UINT32       formatId;
} HplCompressedArgsEntry;

typedef struct {
    UINT32              abandonEntryMagic;
    UINT32              abandonNum;

     /* The format Id that abandoned because task staging buf is not enough to record. */
    HplAbandonedLog     abandonInfo[0];
} HplAbandonedLogEntry;

typedef enum {
    ENTRY_TYPE_FORMAT_FRAGMENT = 0,     /* Indicatas a format dictionary fragment. */
    ENTRY_TYPE_FORMAT_ENTRY,            /* Indicatas a format entry. */
    ENTRY_TYPE_ARGS_FRAGMENT,           /* Indicates a compressed arguments fragment. */
    ENTRY_TYPE_ARGS_ENTRY,              /* Indicatas a arguments entry. */
} HplEntryType;

extern VOID OsHplCompressLog(VOID);
extern VOID OsHplExcCompressLog(VOID);
extern UINT32 OsHplCalcBytes(UINT64 val);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LOS_HPL_COMPRESSION_PRI_H */