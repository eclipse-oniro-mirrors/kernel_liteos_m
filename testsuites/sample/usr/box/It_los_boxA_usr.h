/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the following conditions are met:
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
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _IT_LOS_BOXA_USR_H
#define _IT_LOS_BOXA_USR_H

/* UTS usr-world BOX suite cases: the FILE NAME carries "boxA" on purpose -
 * the boxes.ld selectors route every object whose name matches *boxA*.o
 * into the boxA flash/RAM slots (usr-executable). */

#include "osTest.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

VOID ItLosBoxUsr001(VOID);
VOID ItLosBoxUsr002(VOID);
VOID ItLosBoxUsr003(VOID);
VOID ItLosBoxUsr004(VOID);
VOID ItLosBoxUsr005(VOID);
VOID ItLosBoxUsr006(VOID);
VOID ItLosBoxUsr007(VOID);

/* Memory cases (mem/), one per file. Named like the functional BOX usr
 * cases (ItLosBoxUsr<N>) with a "Mem" tag, so a case name alone tells you
 * it runs in boxA USER mode. Generated from the files present. */
VOID ItLosBoxUsrMem001(VOID);
VOID ItLosBoxUsrMem002(VOID);
VOID ItLosBoxUsrMem003(VOID);
VOID ItLosBoxUsrMem004(VOID);
VOID ItLosBoxUsrMem005(VOID);
VOID ItLosBoxUsrMem006(VOID);
VOID ItLosBoxUsrMem007(VOID);
VOID ItLosBoxUsrMem008(VOID);
VOID ItLosBoxUsrMemC001(VOID);
VOID ItLosBoxUsrMemC006(VOID);
VOID ItLosBoxUsrMemE001(VOID);
VOID ItLosBoxUsrMemF002(VOID);
VOID ItLosBoxUsrMemF003(VOID);
VOID ItLosBoxUsrMemTsk001(VOID);
VOID ItLosBoxUsrMemTsk002(VOID);
VOID ItLosBoxUsrMemTsk003(VOID);
VOID ItLosBoxUsrMemTsk004(VOID);
VOID ItLosBoxUsrMemTsk005(VOID);
VOID ItLosBoxUsrMemTsk006(VOID);
VOID ItLosBoxUsrMemTsk007(VOID);
VOID ItLosBoxUsrMemTsk008(VOID);
VOID ItLosBoxUsrMemTsk009(VOID);
VOID ItLosBoxUsrMemTsk010(VOID);


#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _IT_LOS_BOXA_USR_H */
