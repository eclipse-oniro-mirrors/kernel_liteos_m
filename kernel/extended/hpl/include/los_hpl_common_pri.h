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

#ifndef _LOS_HPL_COMMON_PRI_H
#define _LOS_HPL_COMMON_PRI_H

#include "los_typedef.h"
#include "los_toolchain.h"
#include "securec.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define HPL_MAX_ARGS_NUM                28
#define HPL_ABANDON_MAX_FMT_ID          0xFFFFFFFF

#define HPL_TASK_SLEEP_MS               1000
#define HPL_USER_CUSTOM_STRING_MAGIC    0xCE31BD9A

typedef enum {
    ARGS_TYPE_4_BYTES = 0U,
    ARGS_TYPE_8_BYTES = 1U,
    ARGS_TYPE_STRING = 2U,
    ARGS_TYPE_DOUBLE = 3U, /* Compiler pass float parameter as double type.  */
    ARGS_TYPE_UNSUPPORTED
} HplArgsType;

/*
 * Use 32 bit to indicate format ID and module ID. The high 8 bit indicate the moudle ID which build independent and
 * low 24 bit indicate fmtId.
 * |31                        24|23                      0|
 * |  module number             |        format Id        |
*/
#define HPL_MAX_MODULE_NUM                                  0xFF
#define HPL_MAX_FMT_ID                                      0xFFFFFF
#define HPL_SET_FMTID(fmtId, moduleId)                      ((fmtId) = (fmtId) | ((moduleId) << 24))
#define HPL_GET_MODULE_ID(fmtId)                            (((fmtId) >> 24) & HPL_MAX_MODULE_NUM)
#define HPL_GET_FMT_ID(fmtId)                               ((fmtId) & HPL_MAX_FMT_ID)
#define HPL_SYSTEM_MODULE_ID                                0
/*
 * Use 64 bit to indicate args nums and type. The maximum args number is limited to 28.
 * Each argument use 2 bit to indicate the type.
 *
 * For example:
 * 0x602 -> 0b0110 0000 0010, low 8 bit indicate 2 arguments, high 4 bit indicate that the first arguments
 * type is 0b10, and the second arguments type is 0b01
 *
 * |63 62|61 60|59 58|57 56|55 54|53 52|   ...     |17 16|15 14|13 12|11 10|9 8|7          0|
 * |                           arguments type                                  |    num     |
*/
STATIC INLINE VOID OsHplSetArgsType(UINT32 dynamicNum, UINT32 *argsNum, UINT32 type, UINT64 *typeResult)
{
    for (UINT32 i = 0; i < dynamicNum; i++) {
        /* Each argument use 2 bit to indicate the type */
        (*typeResult) |= ((UINT64)ARGS_TYPE_4_BYTES) << (((*argsNum)++) * 2);
    }
    (*typeResult) |= ((UINT64)type) << (((*argsNum)++) * 2); /* Each argument use 2 bit to indicate the type */
}
#define HPL_SET_ARGS_TYPE_AND_NUM(num, typeResult, analysis)    ((analysis) = (num) | (typeResult) << 8)
#define HPL_GET_ARGS_NUM(analysis)                              ((analysis) & 0xFF)
#define HPL_NEXT_ARGS_TYPE(analysis, i)                         (((analysis) >> (8 + (i) * 2)) & 0x3)

/* Force to 4 bytes align to avoid compiler optimize to 8 bytes align. */
#pragma pack(push, 4)
typedef struct {
    UINT64 timestamp;
    UINT32 fmtId;
} HplAbandonedLog;

typedef struct {
    UINT32 customMagic;
#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
    UINT64 timestamp;
#endif
    UINT32 dataLength;
    UINT8  data[0];
} HplCustomStringHeader;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LOS_HPL_COMMON_PRI_H */