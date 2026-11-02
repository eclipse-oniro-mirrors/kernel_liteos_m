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

/**
 * @defgroup los_hpl High performance log system
 * @ingroup kernel
 */
#ifndef _LOS_HPL_H
#define _LOS_HPL_H

#include "los_typedef.h"
#include "stdarg.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define LOS_HPL_UNASSIGNED_LOGID        0xFFFFFFFF

typedef struct {
    const CHAR *fileName;
    const CHAR *funcName;
    UINT32 line;
} OsHplStaticInfo;

/**
 * @ingroup los_hpl
 * Indicates the buffer size to store log data temporarily. The greater the amount of data, the bigger of buffer size.
 */
#define LOS_HPL_RINGBUF_DEFAULT_SIZE    1024

/**
 * @ingroup los_hpl
 * log system error code: Os run out of memory.
 *
 * Value: 0x02000e01.
 *
 * Solution: Check os memory size.
 * @attention
 * <ul>
 * <li>Please use macros starting with LOS, and macros starting with OS will not be supported.</li>
 * </ul>
 */
#define LOS_ERRNO_HPL_NO_MEMORY                           LOS_ERRNO_OS_ERROR(LOS_MOD_LOG, 0x01)

/**
 * @ingroup los_hpl
 * log system error code: Hpl not created.
 *
 * Value: 0x02000e02.
 *
 * Solution: Create hpl first.
 * @attention
 * <ul>
 * <li>Please use macros starting with LOS, and macros starting with OS will not be supported.</li>
 * </ul>
 */
#define LOS_ERRNO_HPL_NOT_CREATED                         LOS_ERRNO_OS_ERROR(LOS_MOD_LOG, 0x02)

/**
 * @ingroup los_hpl
 * log system error code: Hpl has already created.
 *
 * Value: 0x02000e03.
 *
 * Solution: Never create hpl repeatedly in the same task.
 * @attention
 * <ul>
 * <li>Please use macros starting with LOS, and macros starting with OS will not be supported.</li>
 * </ul>
 */
#define LOS_ERRNO_HPL_HAS_CREATED                         LOS_ERRNO_OS_ERROR(LOS_MOD_LOG, 0x03)

/**
 * @ingroup los_hpl
 * log system error code: Input param invalid.
 *
 * Value: 0x02000e04.
 *
 * Solution: Ensure input params are valid.
 * @attention
 * <ul>
 * <li>Please use macros starting with LOS, and macros starting with OS will not be supported.</li>
 * </ul>
 */
#define LOS_ERRNO_HPL_PARAM_INVALID                       LOS_ERRNO_OS_ERROR(LOS_MOD_LOG, 0x04)

/**
 * @ingroup los_hpl
 * log system error code: Buffer size param invalid.
 *
 * Value: 0x02000e05.
 *
 * Solution: Ensure the staging size in not zero.
 * @attention
 * <ul>
 * <li>Please use macros starting with LOS, and macros starting with OS will not be supported.</li>
 * </ul>
 */
#define LOS_ERRNO_HPL_STAGING_SIZE_INVALID                LOS_ERRNO_OS_ERROR(LOS_MOD_LOG, 0x05)

/**
 * @ingroup los_hpl
 * log system error code: Custom string buffer size overflow.
 *
 * Value: 0x02000e06.
 *
 * Solution: Ensure custom string buffer size no more than 512 Bytes.
 * @attention
 * <ul>
 * <li>Please use macros starting with LOS, and macros starting with OS will not be supported.</li>
 * </ul>
 */
#define LOS_ERRNO_HPL_CUSTOM_SIZE_OVERFLOW                LOS_ERRNO_OS_ERROR(LOS_MOD_LOG, 0x06)

/**
 * @ingroup los_hpl
 * Use this parameter as LOS_HplSetLogLevel function to ignore all log.
 */
#define HPL_SILENT   0

/**
 * @ingroup los_hpl
 * Log level.
 * Bad stuff that shouldn't happen.
 */
#define HPL_ERROR    (1 << 0)

/**
 * @ingroup los_hpl
 * Log level.
 * Messages at HPL_WARNING level indicate something went wrong but it was transient and recoverable.
 */
#define HPL_WARNING  (1 << 1)

/**
 * @ingroup los_hpl
 * Log level.
 * somewhere between HPL_WARNING and HPL_DEBUG
 */
/* */
#define HPL_NOTICE   (1 << 2)

/**
 * @ingroup los_hpl
 * Log level.
 * Messages at HPL_DEBUG level don't indicate that anything went wrong,
 * but they may be useful in diagnosing problems.
 */
#define HPL_DEBUG    (1 << 3)

/**
 * @ingroup  los_hpl
 * @brief HP_LOG macre used for logging.
 *
 * @par Description:
 * This API is used for logging.
 *
 * @attention
 *
 * @param  severity  [IN] Type  #UINT32         log level.
 * @param  format    [IN] Type  #const char *   printf-like format string (must be constant).
 * @param  ...       [IN] Type  #               log arguments associated with the printf-like string.
 *
 * @retval None
 * @par Dependency:
 * <ul><li>los_hpl.h: the header file that contains the API declaration.</li></ul>
 * @see
 */
#ifndef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
#define HP_LOG(severity, format, ...) do { \
    STATIC UINT32 fmtId = LOS_HPL_UNASSIGNED_LOGID; \
    STATIC UINT64 analysis = 0; \
    OsHplStaticInfo fileInfo = { \
        .fileName = __FILE__, \
        .funcName = __FUNCTION__, \
        .line = __LINE__, \
    }; \
    OsHplLogger(&fmtId, &analysis, &fileInfo, severity, format, ##__VA_ARGS__); \
} while (0)
#else
#define HP_LOG(severity, format, ...) do { \
    OsHplLoggerHook(__LINE__, severity, format, ##__VA_ARGS__); \
} while (0)
#endif

/**
 * @ingroup  los_hpl
 * @brief Define the type of log record function.
 *
 * @par Description:
 * This API is used to define the type of log record function.
 * @attention
 *
 * @param  buf  [IN] Type const UINT8 *  The buffer that storage log arguments data.
 * @param  size [IN] Type UINT32         The size of buffer.
 *
 * @retval #LOS_NOK               The buffer is not successfully recorded.
 * @retval #LOS_OK                The buffer is successfully recorded.
 * <ul><li>los_hpl.h: the header file that contains the API declaration.</li></ul>
 * @see
 */
typedef UINT32 (*ARGS_RECORD_FUNC)(const UINT8 *buf, UINT32 size);

/**
 * @ingroup  los_hpl
 * @brief Setting log arguments record hook function.
 *
 * @par Description:
 * This API is used for setting log arguments record hook function.
 *
 * @attention
 *
 * @param recordFunc [IN] Type  #ARGS_RECORD_FUNC   The function to record log arguments, which
 * return LOS_OK indicates success.
 *
 * @retval #LOS_ERRNO_HPL_PARAM_INVALID    The input param is NULL.
 * @retval #LOS_OK                         The log record function is successfully set.
 * <ul><li>los_hpl.h: the header file that contains the API declaration.</li></ul>
 * @see
 */
UINT32 LOS_HplSetLogRecordHook(ARGS_RECORD_FUNC recordFunc);

/**
 * @ingroup  los_hpl
 * @brief Setting log level.
 *
 * @par Description:
 * This API is used for setting log level.
 *
 * @attention
 * Log level is global valid with all task and can be set dynamic.
 *
 * @param level  [IN] Type  #UINTT32       log level.
 *
 * @retval #LOS_OK                         The log level is successfully set.
 * <ul><li>los_hpl.h: the header file that contains the API declaration.</li></ul>
 * @see
 */
UINT32 LOS_HplSetLogLevel(UINT32 level);

#ifdef LOSCFG_KERNEL_HPL_TASK_LOCK_FREE
/**
 * @ingroup  los_hpl
 * @brief Getting the staging buf peak size and overflow status for this task.
 *
 * @par Description:
 * This API is used for getting the staging buf peak size and overflow status for this task.
 *
 * @attention
 *
 * @param  taskId    [OUT] Type  #UINT32      Task ID. The task id value is obtained from task creation.
 * @param  peakSize  [OUT] Type  #UINT32 *    The staging buffer peak size for this task.
 * @param  overflow  [OUT] Type  #BOOL *      The staging buffer overflow status for this task.
 *
 * @retval #LOS_ERRNO_HPL_NOT_CREATED       The task hpl handle not created yet.
 * @retval #LOS_ERRNO_HPL_PARAM_INVALID     The input pointer is NULL.
 * @retval #LOS_OK                          The task staging buffer status is successfully get.
 * @retval #
 * <ul><li>los_hpl.h: the header file that contains the API declaration.</li></ul>
 * @see
 */
UINT32 LOS_TaskHplPeakSizeGet(UINT32 taskId, UINT32 *peakSize, BOOL *overflow);

/**
 * @ingroup  los_hpl
 * @brief Set staging buf size for this task.
 *
 * @par Description:
 * This API is used for creating a high performance log handle for this task.
 *
 * @attention
 * Each task should set staging bufSize to balance memory and performance.
 *
 * @param  taskId   [IN] Type  #UINT32    Task ID. The task id value is obtained from task creation.
 * @param  bufSize  [IN] Type  #UINT32    The buffer size for this task to save log temporarily.
 *
 * @retval #LOS_ERRNO_HPL_HAS_CREATED           Task has already created hpl.
 * @retval #LOS_ERRNO_HPL_NO_MEMORY             The system memory not enough for hpl creating.
 * @retval #LOS_ERRNO_HPL_STAGING_SIZE_INVALID  The param bufSize invalid.
 * @retval #LOS_OK                              The task hpl handle is successfully created.
 * <ul><li>los_hpl.h: the header file that contains the API declaration.</li></ul>
 * @see
 */
UINT32 LOS_TaskHplSizeSet(UINT32 taskId, UINT32 bufSize);
#endif

/**
 * @ingroup  los_hpl
 * @brief Record custom string which has already invoked snprintf by user.
 *
 * @par Description:
 * This API is used for record user custom string which decompressor will not invoke snprintf for this log again.
 *
 * @attention
 *
 * @param  strBuf   [IN] Type  #CHAR *    The buffer that saved character string.
 * @param  bufSize  [IN] Type  #UINT32    The bytes of character string.
 *
 * @retval #LOS_ERRNO_HPL_PARAM_INVALID         The param is 0 or NULL.
 * @retval #LOS_OK                              The task hpl handle is successfully created.
 * <ul><li>los_hpl.h: the header file that contains the API declaration.</li></ul>
 * @see
 */
UINT32 LOS_HplRecordCustomString(CHAR *strBuf, UINT32 bufSize);

#ifdef LOSCFG_KERNEL_HPL_SCRIPT_PREPROCESSOR
extern VOID OsHplLogger(UINT32 severity, va_list arg_ptr, UINT32 fmtId, UINT64 analysis,
                        UINT32 customArgsNum, ...);
#else
extern VOID OsHplLogger(UINT32 *fmtId, UINT64 *analysis, OsHplStaticInfo *fileInfo,
                        UINT32 severity, const CHAR *format, ...);
#endif

extern VOID OsHplLoggerHook(UINT32 severity, UINT32 fmtId, UINT64 analysis, ...);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LOS_HPL_H */
