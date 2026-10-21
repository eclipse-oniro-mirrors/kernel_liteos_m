/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef IT_LOS_DYNLOAD_H
#define IT_LOS_DYNLOAD_H

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#include "iCunit.h"
#include "los_ld_dllib_pri.h"

/* Directory holding the test DSO images; override via compile flags. */
#ifndef DSO_PATH
#define DSO_PATH "/littlefs/dynload/"
#endif
#define DSO_FULL_PATH(p) DSO_PATH p

#define IT_DYNLOAD_WX_ON() do {                                                  \
    UINT32 (*volatile wxFinish)(UINTPTR, UINTPTR, UINTPTR, UINTPTR) = ArchDynloadFinish; \
    (VOID)wxFinish(0, 0, 0, 0);                                                  \
} while (0)

/* Boards whose only executable region for dynamically loaded modules is a dedicated
 * W^X pool window (hi3322: 64KB ITCM window at 0x40000, PMP entry19, registered by
 * board main.c via LOS_DynMemPoolSet at boot; see targets/hispark_hiditing/
 * liteos_m/board/pmp_init.h HI3322_DYNLOAD_POOL_BASE). LOS_LdDestroy() resets the
 * loader heap to the non-executable system heap, so the window must be re-registered
 * before any later "load + call" sequence, otherwise module calls raise an
 * instruction access fault. */
#if defined(LOSCFG_PLATFORM_HI3322)
#define IT_DYNLOAD_WX_POOL_BASE 0x40000u /* keep in sync with HI3322_DYNLOAD_POOL_BASE */
#define IT_DYNLOAD_EXEC_POOL_SET() \
    (VOID)LOS_DynMemPoolSet((VOID *)IT_DYNLOAD_WX_POOL_BASE)
#else
#define IT_DYNLOAD_EXEC_POOL_SET() ((VOID)0)
#endif

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

VOID ItLosDynloadPrepareDsos(VOID);
VOID ItLosDynload001(VOID);
VOID ItLosDynload002(VOID);
VOID ItLosDynload003(VOID);
VOID ItLosDynload004(VOID);
VOID ItLosDynload005(VOID);
VOID ItLosDynload006(VOID);
VOID ItLosDynload007(VOID);
VOID ItLosDynload008(VOID);
VOID ItLosDynload009(VOID);
VOID ItLosDynload010(VOID);
VOID ItLosDynload011(VOID);
VOID ItLosDynload012(VOID);
VOID ItLosDynload013(VOID);
VOID ItLosDynload014(VOID);
VOID ItLosDynload015(VOID);
VOID ItLosDynload016(VOID);
VOID ItLosDynload017(VOID);
VOID ItLosDynload018(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* IT_LOS_DYNLOAD_H */
