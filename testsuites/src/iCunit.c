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

#include "iCunit.h"
#ifdef LOSCFG_BOX
/* UTS single-copy runner (the box_usr_test module compiles this file ONCE,
 * liteos.ld routes the whole object into the COMMON sections - kernel and
 * usr worlds execute the same code): disable the per-case time statistics
 * BEFORE iCunit.inc materializes its state - LOS_SysCycleGet reads the
 * SysTick PPB registers (privileged, kernel flash) and would fault from
 * the usr world. Non-BOX builds are unchanged. */
#include "iCunit_config.h"
#undef ICUNIT_CASE_TIME_STAT
#define ICUNIT_CASE_TIME_STAT 0
#endif
#include "iCunit.inc"
#include "string.h"
#include "osTest.h"
#include "iCunit_config.h"
#include "icunit_filter.h"
#include "los_resleak_test.h"

#include <stdio.h>
#include "securec.h"

#ifdef LOSCFG_BOX
/* UTS single-copy runner continuation:
 *  - OsResLeakCheckBegin/End walk kernel-internal TCB/pool arrays
 *    (los_resleak_test.c stays in the kernel-world test_init module) - the
 *    shared runner must not reach them from usr mode; macro stubs replace
 *    the calls (the kernel phase loses the per-case resleak bracket).
 *  - g_passResult/g_failResult are DEFINED here so they live in COMMON_RAM
 *    with the runner: one shared pair of counters accumulates BOTH the
 *    kernel phase (IT_TST_INI) and the usr phase (box entry). osTest.c
 *    keeps extern-only declarations under BOX. */
#define OsResLeakCheckBegin()       do {} while (0)
#define OsResLeakCheckEnd(caseName) (VOID)(caseName)
/*
 * Pass/fail totals are PER WORLD and merged only when the final report prints.
 *
 * Two boxes running suites concurrently share ICunitRunF, so a single counter
 * pair would be incremented from two contexts with no lock - lost updates on
 * any preemption in between. Keeping one pair per world makes each increment
 * single-writer; ICunitMergeWorldStat() folds them together at report time.
 *
 * The historical names stay usable through these macros, so the bodies of
 * ICunitRunF and the report keep their spelling.
 */
UINT32 g_failResultByWorld[ICUNIT_WORLD_NUM] = {0};
UINT32 g_passResultByWorld[ICUNIT_WORLD_NUM] = {0};

#define g_failResult  (g_failResultByWorld[ICUNIT_WORLD_IDX()])
#define g_passResult  (g_passResultByWorld[ICUNIT_WORLD_IDX()])

/* Read-only views for the report (they must NOT use ICUNIT_WORLD_IDX(), which
 * would resolve against whoever is printing). */
UINT32 ICunitGetPassTotal(VOID)
{
    UINT32 sum = 0;
    UINT32 i;

    for (i = 0; i < ICUNIT_WORLD_NUM; i++) {
        sum += g_passResultByWorld[i];
    }
    return sum;
}

UINT32 ICunitGetFailTotal(VOID)
{
    UINT32 sum = 0;
    UINT32 i;

    for (i = 0; i < ICUNIT_WORLD_NUM; i++) {
        sum += g_failResultByWorld[i];
    }
    return sum;
}

UINT32 ICunitGetWorldPass(UINT32 world)
{
    return (world < ICUNIT_WORLD_NUM) ? g_passResultByWorld[world] : 0U;
}

UINT32 ICunitGetWorldFail(UINT32 world)
{
    return (world < ICUNIT_WORLD_NUM) ? g_failResultByWorld[world] : 0U;
}
#else
extern UINT32 g_failResult;
extern UINT32 g_passResult;
#endif

const char *ICunitLayerToStr(iUINT16 l)
{
    switch (l) {
#define ICUNIT_LAYER(name) case name: return #name;
#include "icunit_layer_def.h"
#undef ICUNIT_LAYER
        default: return "UNKNOWN";
    }
}

const char *ICunitModuleToStr(iUINT16 m)
{
    switch (m) {
#define ICUNIT_MOD(name) case name: return #name;
#include "icunit_module_def.h"
#undef ICUNIT_MOD
        default: return "UNKNOWN";
    }
}

const char *ICunitLevelToStr(iUINT16 l)
{
    switch (l) {
#define ICUNIT_LEVEL(name) case name: return #name;
#include "icunit_level_def.h"
#undef ICUNIT_LEVEL
        default: return "UNKNOWN";
    }
}

const char *ICunitTypeToStr(iUINT16 t)
{
    switch (t) {
#define ICUNIT_TYPE(name) case name: return #name;
#include "icunit_type_def.h"
#undef ICUNIT_TYPE
        default: return "UNKNOWN";
    }
}

/* 模块总数随 icunit_module_def.h 自动同步，避免硬编码下标越界 */
enum {
#define ICUNIT_MOD(name) _MODULE_CNT_##name,
#include "icunit_module_def.h"
#undef ICUNIT_MOD
    MODULE_COUNT_DUMMY
};
#define MODULE_COUNT MODULE_COUNT_DUMMY
/*
 * Per-module statistics, also per world.
 *
 * Same hazard as the totals: two boxes incrementing g_moduleStat[TEST_LOS_USR]
 * without a lock lose updates, and the per-module table is what the final
 * "Executed Model" lines are built from, so the loss would be visible as a
 * short count rather than as an obvious failure.
 *
 * g_moduleStat is the name used in ICunitRunF and in the report; the macro
 * keeps them unchanged.
 */
ICUNIT_MODULE_STAT_S g_moduleStatByWorld[ICUNIT_WORLD_NUM][MODULE_COUNT];

#ifdef ICUNIT_WORLD_IDX
#define g_moduleStat  (g_moduleStatByWorld[ICUNIT_WORLD_IDX()])
#endif

void ICunitSaveErr(iiUINT32 line, iiUINT32 retCode)
{
    iCunit_errLineNo = (iCunit_errLineNo == 0) ? line : iCunit_errLineNo;
    iCunit_errCode = (iCunit_errCode == 0) ? (iiUINT32)retCode : iCunit_errCode;
}

iUINT32 ICunitAddCase(iCHAR *caseName, CASE_FUNCTION caseFunc, iUINT16 testcaseLayer, iUINT16 testcaseModule,
    iUINT16 testcaseLevel, iUINT16 testcaseType)
{
    iUINT16 idx;

    if (iCunit_Init_Success) {
        return (iUINT32)ICUNIT_UNINIT;
    }

    /* 调试过滤：未命中过滤表的用例直接返回，不存表也不执行（遍历但不进入） */
    if (!IcFilterHit(caseName)) {
        return (iUINT32)ICUNIT_SUCCESS;
    }

    /* 崩溃续跑：崩溃点之前的用例直接返回，崩溃点本身跳过并计数，之后正常执行 */
    if (!IcResumeHit(caseName)) {
        return (iUINT32)ICUNIT_SUCCESS;
    }

    idx = 0;
    if (idx == ICUNIT_CASE_SIZE) {
        iCunit_ErrLog_AddCase++;
        return (iUINT32)ICUNIT_CASE_FULL;
    }

    iCunit_CaseArray[idx].pcCaseID = caseName;
    iCunit_CaseArray[idx].pstCaseFunc = caseFunc;
    iCunit_CaseArray[idx].testcase_layer = testcaseLayer;
    iCunit_CaseArray[idx].testcase_module = testcaseModule;
    iCunit_CaseArray[idx].testcase_level = testcaseLevel;
    iCunit_CaseArray[idx].testcase_type = testcaseType;

    ICunitRun();
    return (iUINT32)ICUNIT_SUCCESS;
}

/*
 * Reset the CALLING world's runner state only.
 *
 * The per-world macros above make the scalar fields automatically local, but
 * the three memset_s calls need care: iCunit_CaseArray and g_moduleStat resolve
 * through ICUNIT_WORLD_IDX() so they already hit only this world's copy, while
 * g_failLogArray is still globally shared (one ring buffer for all worlds, its
 * fill count is per-world). Wiping the whole ring from one world would erase
 * another world's recorded failures, so the ring is only cleared when no other
 * world has entries - otherwise this world just resets its own cursor.
 */
iUINT32 ICunitInit()
{
    UINT32 otherLogs = 0;
    UINT32 i;

    iCunit_Init_Success = 0x0000;
    iCunit_Case_Cnt = 0x0000;

    iCunit_Case_FailedCnt = 0;

    iCunit_ErrLog_AddCase = 0;

    (void)memset_s(iCunit_CaseArray, sizeof(iCunit_CaseArray), 0, sizeof(iCunit_CaseArray));

    /* Only clear the shared failure ring if no other world is using it. */
    for (i = 0; i < ICUNIT_WORLD_NUM; i++) {
        if (i != ICUNIT_WORLD_IDX()) {
            otherLogs += g_failLogCountByWorld[i];
        }
    }
    if (otherLogs == 0U) {
        (void)memset_s(g_failLogArray, sizeof(g_failLogArray), 0, sizeof(g_failLogArray));
    }

    (void)memset_s(g_moduleStat, sizeof(g_moduleStat), 0, sizeof(g_moduleStat));
#if (ICUNIT_CASE_TIME_STAT == 1)
    (void)memset_s(g_caseTimeTop, sizeof(g_caseTimeTop), 0, sizeof(g_caseTimeTop));
    g_caseTimeTopCnt = 0;
#endif
    g_failLogCount = 0;

    return (iUINT32)ICUNIT_SUCCESS;
}

void ICunitRecordFailLog(iCHAR *caseID, iUINT16 layer, iUINT16 module, iUINT16 level, iUINT16 type,
    iUINT16 errLine, iiUINT32 retCode)
{
    ICUNIT_FAIL_LOG_S *pLog;
    if (g_failLogCount >= ICUNIT_FAIL_LOG_MAX) {
        return;
    }
    pLog = &g_failLogArray[g_failLogCount];
    pLog->pcCaseID = caseID;
    pLog->testcase_layer = layer;
    pLog->testcase_module = module;
    pLog->testcase_level = level;
    pLog->testcase_type = type;
    pLog->errLine = errLine;
    pLog->retCode = retCode;
    g_failLogCount++;
}

void ICunitPrintFailLogs(void)
{
    iUINT32 i, j, w;
    ICUNIT_FAIL_LOG_S *pLog;

    dprintf("\n========== Failed Case Logs (%d / %d) ==========\n",
            g_failLogCount, ICUNIT_FAIL_LOG_MAX);

    for (i = 0; i < MODULE_COUNT; i++) {
        iUINT32 execCount = 0;
        iUINT32 passCount = 0;
        iUINT32 failCount = 0;

        /* MERGE ACROSS WORLDS.
         *
         * g_moduleStat is a per-world macro (see iCunit.inc), and this function
         * runs in the KERNEL world. Reading g_moduleStat[i] directly would show
         * only the kernel's share and silently drop every box's cases -- which
         * is exactly what happened: the unified report listed TEST_LOS_USR under
         * "Not Executed Model" while success count already contained boxA's 37
         * and boxB's 5 cases. Summing over the worlds keeps the report honest
         * whatever world happens to print it. */
        for (w = 0; w < ICUNIT_WORLD_NUM; w++) {
            execCount += g_moduleStatByWorld[w][i].execCount;
            passCount += g_moduleStatByWorld[w][i].passCount;
            failCount += g_moduleStatByWorld[w][i].failCount;
        }

        if (execCount == 0) {
            continue;
        }

        /* execCount, NOT the module index: the previous version printed `i`,
         * which is why "Executed Model_Num: 62" looked like a doubled count
         * when 62 was simply TEST_LOS_USR's position in the enum. */
        dprintf("Executed Model: %s, Executed Model_Num: %d, failed_count: %d, "
                "success_count: %d\n",
                ICunitModuleToStr((iUINT16)i), execCount, failCount, passCount);

        for (w = 0; w < ICUNIT_WORLD_NUM; w++) {
            for (j = 0; j < g_failLogCountByWorld[w]; j++) {
                pLog = &g_failLogArray[j];
                if (pLog->testcase_module != (iUINT16)i) {
                    continue;
                }
                dprintf("  [Failed]-%s-%s-%s-%s-%s-[Errline: %d RetCode:0x%lx]\n",
                    pLog->pcCaseID,
                    ICunitLayerToStr(pLog->testcase_layer),
                    ICunitModuleToStr(pLog->testcase_module),
                    ICunitLevelToStr(pLog->testcase_level),
                    ICunitTypeToStr(pLog->testcase_type),
                    pLog->errLine, pLog->retCode);
            }
        }
    }

    dprintf("\n Not Executed Model: ");
    for (i = 0; i < MODULE_COUNT; i++) {
        iUINT32 execCount = 0;

        for (w = 0; w < ICUNIT_WORLD_NUM; w++) {
            execCount += g_moduleStatByWorld[w][i].execCount;
        }
        if (execCount != 0) {
            continue;
        }
        dprintf("%s    ", ICunitModuleToStr((iUINT16)i));
    }
    dprintf("\n========== End of Failed Case Logs ==========\n");
}

#if (ICUNIT_CASE_TIME_STAT == 1)
/* 周期数转微秒：先整秒后余数分步换算，避免 64 位溢出与时钟非 1e6 整除的截断误差 */
static iUINT64 ICunitCyclesToUs(UINT64 cycles)
{
    UINT64 sec, rem;
    if (g_sysClock == 0) {
        return 0;
    }
    sec = cycles / g_sysClock;
    rem = cycles % g_sysClock;
    return sec * OS_SYS_US_PER_SECOND + (rem * OS_SYS_US_PER_SECOND) / g_sysClock;
}

/* 按耗时降序维护 TOP 榜：未满直接插入，已满则淘汰末位后重排 */
void ICunitTimeStatRecord(iCHAR *caseID, iUINT64 costUs)
{
    iUINT32 i, j;

    if (g_caseTimeTopCnt < ICUNIT_CASE_TIME_TOP_N) {
        j = g_caseTimeTopCnt;
        g_caseTimeTopCnt++;
    } else {
        if (costUs <= g_caseTimeTop[ICUNIT_CASE_TIME_TOP_N - 1].costUs) {
            return;
        }
        j = ICUNIT_CASE_TIME_TOP_N - 1;
    }
    for (i = j; (i > 0) && (g_caseTimeTop[i - 1].costUs < costUs); i--) {
        g_caseTimeTop[i] = g_caseTimeTop[i - 1];
    }
    g_caseTimeTop[i].pcCaseID = caseID;
    g_caseTimeTop[i].costUs = costUs;
}
#endif

void ICunitPrintTimeStats(void)
{
#if (ICUNIT_CASE_TIME_STAT == 1)
    iUINT32 i;
    iUINT64 us;
    iUINT32 sec, frac;

    dprintf("\n========== Case Cost Time TOP %d ==========\n", g_caseTimeTopCnt);
    for (i = 0; i < g_caseTimeTopCnt; i++) {
        us = g_caseTimeTop[i].costUs;
        sec = (iUINT32)(us / OS_SYS_US_PER_SECOND);
        frac = (iUINT32)(us % OS_SYS_US_PER_SECOND);
        dprintf("TOP%d: %-46s %u.%06u s\n", i + 1, g_caseTimeTop[i].pcCaseID, sec, frac);
    }
    dprintf("========== End of Case Cost Time TOP ==========\n");
#endif
}

iUINT32 ICunitRunF(void)
{
    iUINT32 idx, idx1;
    ICUNIT_CASE_S *psubCaseArray;
    iUINT32 caseRet;
#if (ICUNIT_CASE_TIME_STAT == 1)
    UINT64 startCycles;
    iUINT64 costUs;
#endif

    psubCaseArray = iCunit_CaseArray;
    idx1 = 1;

    for (idx = 0; idx < idx1; idx++, psubCaseArray++) {
        iCunit_errLineNo = 0;
        iCunit_errCode = 0;

        OsResLeakCheckBegin();
#if (ICUNIT_CASE_TIME_STAT == 1)
        startCycles = LOS_SysCycleGet();
#endif
        caseRet = psubCaseArray->pstCaseFunc();
#if (ICUNIT_CASE_TIME_STAT == 1)
        costUs = ICunitCyclesToUs(LOS_SysCycleGet() - startCycles);
        ICunitTimeStatRecord(psubCaseArray->pcCaseID, costUs);
        if (costUs > ICUNIT_CASE_TIME_SLOW_US) {
            dprintf("[SlowCase]-%s-[Cost: %u.%06u s]\n", psubCaseArray->pcCaseID,
                (iUINT32)(costUs / OS_SYS_US_PER_SECOND), (iUINT32)(costUs % OS_SYS_US_PER_SECOND));
        }
#endif
        psubCaseArray->errLine = iCunit_errLineNo;
        psubCaseArray->retCode = (0 == iCunit_errLineNo) ? (caseRet) : (iCunit_errCode);

        if (0 == iCunit_errLineNo && 0 == caseRet) {
            g_passResult++;
            if (psubCaseArray->testcase_module < MODULE_COUNT) {
                g_moduleStat[psubCaseArray->testcase_module].execCount++;
                g_moduleStat[psubCaseArray->testcase_module].passCount++;
            }
            dprintf("  [Passed]-%s-%s-%s-%s-%s\n", psubCaseArray->pcCaseID,
                ICunitLayerToStr(psubCaseArray->testcase_layer),
                ICunitModuleToStr(psubCaseArray->testcase_module),
                ICunitLevelToStr(psubCaseArray->testcase_level),
                ICunitTypeToStr(psubCaseArray->testcase_type));
        } else {
            g_failResult++;
            iCunit_Case_FailedCnt++;
            if (psubCaseArray->testcase_module < MODULE_COUNT) {
                g_moduleStat[psubCaseArray->testcase_module].execCount++;
                g_moduleStat[psubCaseArray->testcase_module].failCount++;
            }
            dprintf("  [Failed]-%s-%s-%s-%s-%s-[Errline: %d RetCode:0x%lx]\n", psubCaseArray->pcCaseID,
                ICunitLayerToStr(psubCaseArray->testcase_layer),
                ICunitModuleToStr(psubCaseArray->testcase_module),
                ICunitLevelToStr(psubCaseArray->testcase_level),
                ICunitTypeToStr(psubCaseArray->testcase_type),
                psubCaseArray->errLine, psubCaseArray->retCode);
            ICunitRecordFailLog(psubCaseArray->pcCaseID, psubCaseArray->testcase_layer,
                psubCaseArray->testcase_module, psubCaseArray->testcase_level, psubCaseArray->testcase_type,
                psubCaseArray->errLine, psubCaseArray->retCode);
        }
        OsResLeakCheckEnd(psubCaseArray->pcCaseID);
    }

    return (iUINT32)ICUNIT_SUCCESS;
}

iUINT32 ICunitRun()
{
    if (iCunit_Init_Success) {
        return (iUINT32)ICUNIT_UNINIT;
    }

    ICunitRunF();

    return (iUINT32)ICUNIT_SUCCESS;
}

