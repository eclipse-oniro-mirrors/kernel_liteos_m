/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
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


#ifndef _ICUNIT_ICUNIT_INC
#define _ICUNIT_ICUNIT_INC

#include "iCunit_config.h"

/*
 * World discrimination for the per-world state below.
 *
 * These two come from los_box_pri.h, which cannot be included here: it is a
 * kernel header and this file is compiled into the usr world too (box_usr_test
 * module), where the kernel include root is deliberately unreachable. So the
 * one value and one variable we actually need are declared locally - the same
 * trick los_memory_pri.h uses for g_sysMode.
 *
 * g_curBoxId lives in USER_READ_ONLY_RAM (R2): kernel-writable, readable by
 * every box. That is what makes the index computation legal in BOTH worlds
 * without a syscall or a kernel call.
 *
 * Non-BOX builds never take this branch: without LOSCFG_BOX there is one world
 * and the macros below are not defined at all.
 */
#ifdef LOSCFG_BOX
#ifndef OS_KERNEL_BOX
#define OS_KERNEL_BOX                   0xFFU
#endif
extern volatile UINT32 g_curBoxId;
#endif

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */


/*
 * ---------------------------------------------------------------------------
 * PER-WORLD TEST STATE (kernel / boxA / boxB)
 * ---------------------------------------------------------------------------
 * iCunit was written for a single runner: every piece of mutable state below
 * used to be ONE variable. That breaks the moment two boxes run suites at the
 * same time - and it breaks SILENTLY, which is the dangerous part:
 *
 *     boxA: iCunit_errLineNo = 0;  case();   // assertion fails -> errLine = 100
 *     boxB: iCunit_errLineNo = 0;  case();   // clears it back to 0
 *     boxA: if (errLine == 0) g_passResult++;   // FAILURE REPORTED AS PASS
 *
 * So each world gets its own copy. The index comes from g_curBoxId, which lives
 * in the R2 window (USER_READ_ONLY_RAM) and is therefore readable from user
 * mode without calling anything:
 *
 *     g_curBoxId == OS_KERNEL_BOX (0xFF)  ->  world 0  (kernel)
 *     g_curBoxId == 0                     ->  world 1  (boxA)
 *     g_curBoxId == 1                     ->  world 2  (boxB)
 *
 * NOTE OsGetCurBoxCB() would be the natural helper but it lives in
 * KERNEL_FLASH (0x0800621C on this target), and calling it from a box is an
 * IACCVIOL. Reading g_curBoxId directly is the only user-safe way.
 *
 * MACRO TRICK: the macros below map the historical names onto "the current
 * world's slot", so the bodies of ICunitAddCase / ICunitRunF / ICunitInit keep
 * their original spelling. Only the definitions and the report merge needed to
 * change. That keeps the diff reviewable - this file is shared framework code
 * used by the kernel phase too.
 *
 * ICUNIT_CASE_S per world is 1, not ICUNIT_CASE_SIZE: ICunitAddCase runs the
 * case IMMEDIATELY and always uses slot 0 (idx = 0 is hard-coded), so a slot
 * only ever holds one case at a time. Keeping ICUNIT_CASE_SIZE copies would
 * cost 3 x ICUNIT_CASE_SIZE x sizeof(ICUNIT_CASE_S) of COMMON_RAM - tens of KB
 * that this 16K window cannot spare.
 */
#ifdef LOSCFG_BOX
/* Per-world state only makes sense with more than one world. */
#define ICUNIT_WORLD_NUM   3

/* world index for the CURRENT context; safe in both worlds */
#define ICUNIT_WORLD_IDX()                                                   \
    ((g_curBoxId == OS_KERNEL_BOX) ? 0U : (((UINT32)g_curBoxId + 1U) % ICUNIT_WORLD_NUM))

ICUNIT_CASE_S iCunit_CaseArrayByWorld[ICUNIT_WORLD_NUM][1];
ICUNIT_SUIT_S iCunit_SuitArray[ICUNIT_SUIT_SIZE];

iUINT32 iCunit_Init_SuccessByWorld[ICUNIT_WORLD_NUM];
iUINT32 iCunit_Case_CntByWorld[ICUNIT_WORLD_NUM];
iUINT32 iCunit_Suit_Cnt = 0xFFFF;
iUINT32 iCunit_Case_IdxByWorld[ICUNIT_WORLD_NUM];
iUINT32 iCunit_Suit_Idx = 0xFFFF;
iUINT32 iCunit_Case_FailedCntByWorld[ICUNIT_WORLD_NUM];
iUINT32 iCunit_Suit_FailedCnt = 0;
iUINT32 iCunit_ErrLog_AddSuit = 0;
iUINT32 iCunit_ErrLog_AddCaseByWorld[ICUNIT_WORLD_NUM];

iUINT16 iCunit_errLineNoByWorld[ICUNIT_WORLD_NUM];
iiUINT32 iCunit_errCodeByWorld[ICUNIT_WORLD_NUM];

ICUNIT_FAIL_LOG_S g_failLogArray[ICUNIT_FAIL_LOG_MAX];
iUINT32 g_failLogCountByWorld[ICUNIT_WORLD_NUM];

#if (ICUNIT_CASE_TIME_STAT == 1)
ICUNIT_TIME_STAT_S g_caseTimeTop[ICUNIT_CASE_TIME_TOP_N];
iUINT32 g_caseTimeTopCntByWorld[ICUNIT_WORLD_NUM];
#endif

/* Bind the historical names to the calling world's slot. Anything that reads or
 * writes these names is automatically per-world; nothing else in iCunit.c had
 * to change. */
#define iCunit_CaseArray       (iCunit_CaseArrayByWorld[ICUNIT_WORLD_IDX()])
#define iCunit_Init_Success    (iCunit_Init_SuccessByWorld[ICUNIT_WORLD_IDX()])
#define iCunit_Case_Cnt        (iCunit_Case_CntByWorld[ICUNIT_WORLD_IDX()])
#define iCunit_Case_Idx        (iCunit_Case_IdxByWorld[ICUNIT_WORLD_IDX()])
#define iCunit_Case_FailedCnt  (iCunit_Case_FailedCntByWorld[ICUNIT_WORLD_IDX()])
#define iCunit_ErrLog_AddCase  (iCunit_ErrLog_AddCaseByWorld[ICUNIT_WORLD_IDX()])
#define iCunit_errLineNo       (iCunit_errLineNoByWorld[ICUNIT_WORLD_IDX()])
#define iCunit_errCode         (iCunit_errCodeByWorld[ICUNIT_WORLD_IDX()])
#define g_failLogCount         (g_failLogCountByWorld[ICUNIT_WORLD_IDX()])
#if (ICUNIT_CASE_TIME_STAT == 1)
#define g_caseTimeTopCnt       (g_caseTimeTopCntByWorld[ICUNIT_WORLD_IDX()])
#endif
#else  /* !LOSCFG_BOX */
/* Single world: ICUNIT_WORLD_NUM 1, index always 0. */
#define ICUNIT_WORLD_NUM   1
#define ICUNIT_WORLD_IDX() 0U
#endif /* LOSCFG_BOX */

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */


#endif /* _UNI_ICUNIT_INC */
