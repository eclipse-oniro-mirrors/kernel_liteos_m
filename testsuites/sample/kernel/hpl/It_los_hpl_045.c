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
 * 3. Neither the name of the copyright holder nor any of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written
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

#include "osTest.h"
#include "It_los_hpl.h"
#include "los_hpl_logger_pri.h"
#include "los_task_pri.h"

/* 用例简要描述: HPL无记录钩子压缩周期早退缺陷受控复现与自愈测试——问题20261102_165607的复现载体：无hook窗口下preDelete暂存不释放（S1泄漏）、格式环形缓冲填满后新站点静默拒绝（S2丢弃）、checkpoint永不写入（S3），安装hook后暂存释放/缓冲条目补投/被拒站点重试成功（S4自愈） */

/* Controlled reproduction carrier for problem record 20261102_165607:
 * with recordHook NULL the periodic compression cycle bails out at the boot
 * checkpoint write (goto ERR) and skips the whole cycle body, so (S1) the
 * preDelete'd staging buffer of a deleted task is never freed, (S2) the
 * format ring buffer is never drained and new call sites are silently
 * rejected once it fills, (S3) the checkpoint is never written. Installing
 * any hook later recovers everything (S4): the buffered entries are
 * delivered, the preDelete'd buffer is freed and the rejected sites
 * succeed on their next call through the same static fmtId.
 *
 * Reproduction state: log level OPEN + recordHook NULL. The level half is
 * set explicitly at case start (the boot default is HPL_SILENT, which
 * filters every HP_LOG at the severity check long before the assistant);
 * the hook half is the boot default (no product code installs one, and the
 * public API rejects NULL so a hook cannot be uninstalled afterwards).
 *
 * This case MUST stay the FIRST case of the HPL suite: the no-hook state
 * only exists from boot until the first hook installer. The white-box
 * precondition assert below fails loudly if this ordering is ever violated.
 *
 * Sentinel note: the S1 defect-era assert (buffer stays inited at the poll
 * cap) flips to "freed" once the product side decouples the reclaim scan
 * from the sink (problem record fix plan a); update it together with the
 * fix. The S2 rejection assert stays valid under fix plan a: draining the
 * format entries still requires an output sink. */

#define HPL_045_FILL_SITES     64   /* HPL_LOGGER_FMT_BUF_SIZE / sizeof(HplStaticInfo) = 2048/32 */
#define HPL_045_OVER_SITES     2    /* sites probed past the ring capacity */
#define HPL_045_SITES          (HPL_045_FILL_SITES + HPL_045_OVER_SITES)
#define HPL_045_W1_STAGING     1024
#define HPL_045_W2_STAGING     4096 /* keeps every fill entry staged, no overflow -> no abandoned */
#define HPL_045_WAIT_CAP       240

STATIC volatile UINT32 g_gate45a;   /* S1 worker release */
STATIC volatile UINT32 g_gate45b;   /* S2 worker phase B release */
STATIC volatile UINT32 g_probe45Done;
STATIC UINT32 g_probe45Fmt[HPL_045_SITES]; /* recorded fmtId per probe site */

/* Probe through the runtime preprocessor exactly like HP_LOG, keeping the
 * per-site STATIC fmtId so the case can observe the assistant verdict. */
#define HPL45_PROBE(siteIdx, fmt, ...)                                        \
    do {                                                                      \
        STATIC UINT32 hpl45FmtId = LOS_HPL_UNASSIGNED_LOGID;                   \
        UINT64 hpl45Ana = 0;                                                  \
        OsHplStaticInfo hpl45Info = {                                         \
            .fileName = __FILE__,                                             \
            .funcName = __FUNCTION__,                                         \
            .line = __LINE__,                                                 \
        };                                                                    \
        OsHplLogger(&hpl45FmtId, &hpl45Ana, &hpl45Info, HPL_DEBUG, fmt,        \
                    ##__VA_ARGS__);                                            \
        g_probe45Fmt[siteIdx] = hpl45FmtId;                                   \
    } while (0)

/* S1 worker: creates a staging buffer, exits (DETACHED auto-delete) so its
 * buffer is only reclaimable through the compression-cycle handle scan. */
static VOID TaskF45a(VOID)
{
    while (g_gate45a == 0) {
        LOS_TaskDelay(1);
    }
}

/* S2 worker: phase A probes HPL_045_FILL_SITES unique sites (the format ring
 * is never drained without a hook, so it fills to capacity) plus two
 * over-capacity sites that must be silently rejected; phase B (after the
 * hook is installed and the ring drained) retries the two rejected sites
 * through their original expansions -- the per-site STATIC fmtId is still
 * unassigned, so the retry re-runs the assistant and must succeed. */
static VOID TaskF45b(VOID)
{
    UINT32 phase;

    (VOID)LOS_TaskHplSizeSet(LOS_CurTaskIDGet(), HPL_045_W2_STAGING);
    for (phase = 0; phase < 2; phase++) { /* 2: phase A + phase B retry */
        if (phase == 0) {
            HPL45_PROBE(0, "hpl45 s00 %u", 0);
            HPL45_PROBE(1, "hpl45 s01 %u", 1);
            HPL45_PROBE(2, "hpl45 s02 %u", 2);
            HPL45_PROBE(3, "hpl45 s03 %u", 3);
            HPL45_PROBE(4, "hpl45 s04 %u", 4);
            HPL45_PROBE(5, "hpl45 s05 %u", 5);
            HPL45_PROBE(6, "hpl45 s06 %u", 6);
            HPL45_PROBE(7, "hpl45 s07 %u", 7);
            HPL45_PROBE(8, "hpl45 s08 %u", 8);
            HPL45_PROBE(9, "hpl45 s09 %u", 9);
            HPL45_PROBE(10, "hpl45 s10 %u", 10);
            HPL45_PROBE(11, "hpl45 s11 %u", 11);
            HPL45_PROBE(12, "hpl45 s12 %u", 12);
            HPL45_PROBE(13, "hpl45 s13 %u", 13);
            HPL45_PROBE(14, "hpl45 s14 %u", 14);
            HPL45_PROBE(15, "hpl45 s15 %u", 15);
            HPL45_PROBE(16, "hpl45 s16 %u", 16);
            HPL45_PROBE(17, "hpl45 s17 %u", 17);
            HPL45_PROBE(18, "hpl45 s18 %u", 18);
            HPL45_PROBE(19, "hpl45 s19 %u", 19);
            HPL45_PROBE(20, "hpl45 s20 %u", 20);
            HPL45_PROBE(21, "hpl45 s21 %u", 21);
            HPL45_PROBE(22, "hpl45 s22 %u", 22);
            HPL45_PROBE(23, "hpl45 s23 %u", 23);
            HPL45_PROBE(24, "hpl45 s24 %u", 24);
            HPL45_PROBE(25, "hpl45 s25 %u", 25);
            HPL45_PROBE(26, "hpl45 s26 %u", 26);
            HPL45_PROBE(27, "hpl45 s27 %u", 27);
            HPL45_PROBE(28, "hpl45 s28 %u", 28);
            HPL45_PROBE(29, "hpl45 s29 %u", 29);
            HPL45_PROBE(30, "hpl45 s30 %u", 30);
            HPL45_PROBE(31, "hpl45 s31 %u", 31);
            HPL45_PROBE(32, "hpl45 s32 %u", 32);
            HPL45_PROBE(33, "hpl45 s33 %u", 33);
            HPL45_PROBE(34, "hpl45 s34 %u", 34);
            HPL45_PROBE(35, "hpl45 s35 %u", 35);
            HPL45_PROBE(36, "hpl45 s36 %u", 36);
            HPL45_PROBE(37, "hpl45 s37 %u", 37);
            HPL45_PROBE(38, "hpl45 s38 %u", 38);
            HPL45_PROBE(39, "hpl45 s39 %u", 39);
            HPL45_PROBE(40, "hpl45 s40 %u", 40);
            HPL45_PROBE(41, "hpl45 s41 %u", 41);
            HPL45_PROBE(42, "hpl45 s42 %u", 42);
            HPL45_PROBE(43, "hpl45 s43 %u", 43);
            HPL45_PROBE(44, "hpl45 s44 %u", 44);
            HPL45_PROBE(45, "hpl45 s45 %u", 45);
            HPL45_PROBE(46, "hpl45 s46 %u", 46);
            HPL45_PROBE(47, "hpl45 s47 %u", 47);
            HPL45_PROBE(48, "hpl45 s48 %u", 48);
            HPL45_PROBE(49, "hpl45 s49 %u", 49);
            HPL45_PROBE(50, "hpl45 s50 %u", 50);
            HPL45_PROBE(51, "hpl45 s51 %u", 51);
            HPL45_PROBE(52, "hpl45 s52 %u", 52);
            HPL45_PROBE(53, "hpl45 s53 %u", 53);
            HPL45_PROBE(54, "hpl45 s54 %u", 54);
            HPL45_PROBE(55, "hpl45 s55 %u", 55);
            HPL45_PROBE(56, "hpl45 s56 %u", 56);
            HPL45_PROBE(57, "hpl45 s57 %u", 57);
            HPL45_PROBE(58, "hpl45 s58 %u", 58);
            HPL45_PROBE(59, "hpl45 s59 %u", 59);
            HPL45_PROBE(60, "hpl45 s60 %u", 60);
            HPL45_PROBE(61, "hpl45 s61 %u", 61);
            HPL45_PROBE(62, "hpl45 s62 %u", 62);
            HPL45_PROBE(63, "hpl45 s63 %u", 63);
        }
        /* The two over-capacity sites: original expansions, probed in BOTH
         * phases so the phase B retry goes through the same static fmtId. */
        HPL45_PROBE(64, "hpl45 over a %u", 64);
        HPL45_PROBE(65, "hpl45 over b %u", 65);
        if (phase == 0) {
            g_probe45Done = 1;
            while (g_gate45b == 0) {
                LOS_TaskDelay(1);
            }
        }
    }
    g_testCount = 2;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 i;
    UINT32 wait;
    UINT32 assigned = 0;
    UINT32 argsBefore;
    UINT32 fmtBefore;
    UINT32 ckptBefore;
    HplHandle *hpl = NULL;
    TSK_INIT_PARAM_S taskParam = {0};

    /* Ordering guard: the no-hook window only exists before the first hook
     * installer. If this assert fires, this case lost its first position. */
    ICUNIT_ASSERT_EQUAL((g_runtimeLogger.recordHook == NULL), 1, 1);
    /* Open the level only (TestHplPrepare's other half): the boot default
     * HPL_SILENT would filter every probe at the severity check. */
    ret = LOS_HplSetLogLevel(HPL_TEST_LOG_LEVEL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* ---- S1: staging of a deleted task is not reclaimed without a hook. */
    g_gate45a = 0;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF45a;
    taskParam.pcName = "HplRp45a";
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_TaskHplSizeSet(g_testTaskID01, HPL_045_W1_STAGING);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* ---- S2: the format ring is never drained, new sites get rejected.
     * W2 is created (and completes phase A) BEFORE W1 is released, so W2
     * cannot take W1's recycled slot and the S1 poll below reads W1's TCB. */
    g_gate45b = 0;
    g_probe45Done = 0;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF45b;
    taskParam.pcName = "HplRp45b";
    ret = LOS_TaskCreate(&g_testTaskID02, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    wait = 0;
    while ((g_probe45Done != 1) && (wait < HPL_045_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_probe45Done, 1, g_probe45Done);

    /* Now release W1: it exits (DETACHED auto-delete) and its staging can
     * only be reclaimed through the compression-cycle handle scan. */
    g_gate45a = 1;

    argsBefore = g_hplArgsEntryCnt;
    fmtBefore = g_hplFmtEntryCnt;
    ckptBefore = g_hplCheckpointCnt;

    /* Defect observables, phase A (no hook):
     * 1) S1: let at least one full compression cycle (100 ticks) pass and
     *    assert the deleted task's staging is still not freed;
     * 2) S2: the fill sites are assigned, the over-capacity sites are
     *    silently rejected (fmtId stays unassigned) -- dynamic prefix
     *    analysis so the assert does not depend on sizeof(HplStaticInfo). */
    wait = 0;
    do {
        LOS_TaskDelay(1);
        wait++;
        hpl = (HplHandle *)OS_TCB_FROM_TID(g_testTaskID01)->hpl;
    } while ((hpl != NULL) && (hpl->inited == TRUE) && (wait < HPL_045_WAIT_CAP));
    ICUNIT_ASSERT_EQUAL((hpl != NULL) && (hpl->inited == TRUE), 1, wait); /* S1 defect-era */
    ICUNIT_ASSERT_EQUAL(g_hplCheckpointCnt - ckptBefore, 0, g_hplCheckpointCnt - ckptBefore); /* S3 */

    for (i = 0; i < HPL_045_SITES; i++) {
        if (g_probe45Fmt[i] != LOS_HPL_UNASSIGNED_LOGID) {
            assigned++;
        }
    }
    /* prefix property: assigned sites form a prefix, rejected a suffix */
    for (i = 0; i < assigned; i++) {
        ICUNIT_ASSERT_EQUAL((g_probe45Fmt[i] != LOS_HPL_UNASSIGNED_LOGID), 1, i);
    }
    for (i = assigned; i < HPL_045_SITES; i++) {
        ICUNIT_ASSERT_EQUAL(g_probe45Fmt[i], LOS_HPL_UNASSIGNED_LOGID, i);
    }
    ICUNIT_ASSERT_EQUAL((assigned >= HPL_045_FILL_SITES / 2), 1, assigned); /* ring really filled */
    ICUNIT_ASSERT_EQUAL((HPL_045_SITES - assigned) >= HPL_045_OVER_SITES, 1,
                        HPL_045_SITES - assigned); /* rejection really happened */

    /* ---- S4: recovery once any hook is installed. */
    TestHplPrepare();
    wait = 0;
    while ((g_hplArgsEntryCnt - argsBefore) < assigned && (wait < HPL_045_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_hplArgsEntryCnt - argsBefore, assigned,
                        g_hplArgsEntryCnt - argsBefore); /* buffered entries delivered */
    ICUNIT_ASSERT_EQUAL(g_hplFmtEntryCnt - fmtBefore, assigned,
                        g_hplFmtEntryCnt - fmtBefore); /* fmt ring drained */
    ICUNIT_ASSERT_EQUAL((g_hplCheckpointCnt - ckptBefore) >= 1, 1,
                        g_hplCheckpointCnt - ckptBefore); /* checkpoint written */
    hpl = (HplHandle *)OS_TCB_FROM_TID(g_testTaskID01)->hpl;
    ICUNIT_ASSERT_EQUAL((hpl != NULL) && (hpl->inited == FALSE), 1, wait); /* S1 recovered */

    /* Phase B: the rejected sites retry through their original statics and
     * must now be accepted and delivered. */
    g_testCount = 0;
    g_gate45b = 1;
    wait = 0;
    while ((g_testCount != 2) && (wait < HPL_045_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 2, g_testCount);
    ICUNIT_ASSERT_EQUAL((g_probe45Fmt[64] != LOS_HPL_UNASSIGNED_LOGID), 1, g_probe45Fmt[64]);
    ICUNIT_ASSERT_EQUAL((g_probe45Fmt[65] != LOS_HPL_UNASSIGNED_LOGID), 1, g_probe45Fmt[65]);
    wait = 0;
    while ((g_hplArgsEntryCnt - argsBefore) < (assigned + HPL_045_OVER_SITES) &&
           (wait < HPL_045_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_hplArgsEntryCnt - argsBefore, assigned + HPL_045_OVER_SITES,
                        g_hplArgsEntryCnt - argsBefore);
    ICUNIT_ASSERT_EQUAL(g_hplFmtEntryCnt - fmtBefore, assigned + HPL_045_OVER_SITES,
                        g_hplFmtEntryCnt - fmtBefore);

    /* W2 exited (DETACHED): its staging must be reclaimed as well. */
    wait = 0;
    do {
        LOS_TaskDelay(1);
        wait++;
        hpl = (HplHandle *)OS_TCB_FROM_TID(g_testTaskID02)->hpl;
    } while ((hpl != NULL) && (hpl->inited == TRUE) && (wait < HPL_045_WAIT_CAP));
    ICUNIT_ASSERT_EQUAL((hpl != NULL) && (hpl->inited == FALSE), 1, wait);

    TestHplPrepare();
    return LOS_OK;
}

VOID ItLosHpl045(VOID)
{
    TEST_ADD_CASE("ItLosHpl045", TestCase, TEST_LOS, TEST_FN_KERNELEXT_HPL, TEST_LEVEL0, TEST_FUNCTION);
}
