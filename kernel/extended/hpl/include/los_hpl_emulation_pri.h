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

#ifndef _LOS_HPL_EMULATION_PRI_H
#define _LOS_HPL_EMULATION_PRI_H

#include "los_typedef.h"
#include "los_hpl_logger_pri.h"
#include "los_hpl_decompression_pri.h"
#include "los_hpl_compression_pri.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define HPL_DECOMPRESS_THREAD_PRIORITY            10
#define HPL_EMULATION_SHARE_SIZE                  (4096 * 4)

extern UINT32 OsHplEmulationInit(VOID);
extern UINT32 OsHplEmulationWrite(HplRuntimeLogger *logger, const UINT8 *buf, UINT32 size);
extern VOID OsHplEmulationDeinit(VOID);
extern VOID OsHplExcDecompressLog(VOID);

#ifdef LOSCFG_OBSOLETE_API
extern VOID *OsHplDecompressMain(UINTPTR priv, UINTPTR param1, UINTPTR param2, UINTPTR param3);
#else
extern VOID *OsHplDecompressMain(VOID *priv);
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LOS_HPL_EMULATION_PRI_H */
