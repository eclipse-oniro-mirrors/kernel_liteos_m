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

#ifndef IT_LOS_LMS_H
#define IT_LOS_LMS_H

#include "osTest.h"
#include "los_memory.h"
#include "los_config.h"
#include "iCunit.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#define INDEX_MAX                        20
#define PAGE_SIZE                        (0x1000U)

extern char g_testLmsPool[2 * PAGE_SIZE];

VOID ItLosLms001(void);
VOID ItLosLms002(void);
VOID ItLosLms003(void);
VOID ItLosLms004(void);
VOID ItLosLms005(void);
VOID ItLosLms006(void);
VOID ItLosLms007(void);
VOID ItLosLms008(void);
VOID ItLosLms009(void);
VOID ItLosLms010(void);
VOID ItLosLms011(void);
VOID ItLosLms012(void);
VOID ItLosLms013(void);
VOID ItLosLms014(void);
VOID ItLosLms015(void);
VOID ItLosLms016(void);
VOID ItLosLms017(void);
VOID ItLosLms018(void);
VOID ItLosLms019(void);
VOID ItLosLms020(void);
VOID ItLosLms021(void);
VOID ItLosLms022(void);
VOID ItLosLms023(void);
VOID ItLosLms024(void);
/* LMS compatibility & new feature coverage tests */
VOID ItLosLms025(void);
VOID ItLosLms026(void);
VOID ItLosLms027(void);
VOID ItLosLms028(void);
VOID ItLosLms029(void);
VOID ItLosLms030(void);
VOID ItLosLms031(void);
/* LMS pool-table/shadow/report edge coverage tests */
VOID ItLosLms032(void);
VOID ItLosLms033(void);
VOID ItLosLms034(void);
VOID ItLosLms035(void);
VOID ItLosLms036(void);
VOID ItLosLms037(void);
VOID ItLosLms038(void);
/* BUG_09 缺陷复现用例(默认隔离): 隔离点在 It_los_lms.c 聚合注册点,
 * 宏 KERNEL_BUG_09_LMS_ERRINFO_NULL 统一定义在 bug_isolate.h,
 * 见 It_los_lms_039.c 头注释 */
VOID ItLosLms039(void);
/* BUG_10 缺陷回归看护用例(默认隔离): 隔离点在 It_los_lms.c 聚合注册点,
 * 宏 KERNEL_BUG_10_LMS_ADDPOOL_DEADNODE 统一定义在 bug_isolate.h,
 * 见 It_los_lms_040.c 头注释 */
VOID ItLosLms040(void);

#if defined(LOSCFG_PLATFORM_WS63_M) && defined(LOSCFG_KERNEL_LMS)
/* LMS 错误报告在关中断状态下打印 backtrace+mem dump(gcov 插桩下单份约 4s),
 * 期间 tick 大量丢失, 喂狗任务 app_task 无法按 100 tick 周期唤醒, 连续报告
 * 会耗尽 60s 看门狗窗口导致单板复位。在用例边界/报告组之间直接调用板级
 * 喂狗接口(app_task 喂的是同一个看门狗), 不依赖调度恢复。仅 WS63 且开启
 * LMS 时生效, 其它平台/配置编译为空。 */
extern void ws63_watchdog_feed(void);
#define LMS_FEED_WDT() ws63_watchdog_feed()
#else
#define LMS_FEED_WDT()
#endif

/* ==================== LMS Test Sandbox ==================== */
#include "los_lms_pri.h"

#define LMS_TEST_SANDBOX_SIZE  (2 * PAGE_SIZE)
extern CHAR g_lmsSandboxBuf[LMS_TEST_SANDBOX_SIZE];
extern BOOL g_lmsSandboxFirstCall;

typedef UINT32 (*LmsTestImplFn)(VOID *pool);

static inline UINT32 LmsTestRunInSandbox(LmsTestImplFn impl, UINT32 poolSize)
{
    UINT32 ret;

    if (poolSize > LMS_TEST_SANDBOX_SIZE) {
        poolSize = LMS_TEST_SANDBOX_SIZE;
    }
    if (!g_lmsSandboxFirstCall) {
        /* DeInit must run BEFORE the wipe: LOS_MemDeInit unregisters the pool
         * from the LMS check list first, so the memset below is no longer
         * LMS-checked. Wiping first would store into the still-registered
         * pool's PAINT-marked header ([pool, firstNode)) and emit a spurious
         * "UnKnown Error / Illegal WRITE" report at every sandbox re-init
         * (11 reports on WS63, 14 on QEMU; each dumps backtrace+memory with
         * interrupts locked for ~4s under gcov, risking watchdog reset). */
        (VOID)LOS_MemDeInit(g_lmsSandboxBuf);
        (VOID)memset_s(g_lmsSandboxBuf, LMS_TEST_SANDBOX_SIZE, 0, LMS_TEST_SANDBOX_SIZE);
    }
    g_lmsSandboxFirstCall = FALSE;
#ifdef LOSCFG_KERNEL_MEM_SLAB_EXTENTION
    if (LOS_MemPoolInit(g_lmsSandboxBuf, poolSize, 0) != LOS_OK) {
        return LOS_NOK;
    }
#else
    if (LOS_MemInit(g_lmsSandboxBuf, poolSize) != LOS_OK) {
        return LOS_NOK;
    }
#endif
    ret = impl(g_lmsSandboxBuf);
    OsLmsErrorHookSet(NULL);
    return ret;
}

#define LMS_TEST_RUN_IN_SANDBOX(impl_fn, pool_size) \
    LmsTestRunInSandbox((impl_fn), (pool_size))

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* IT_LOS_LMS_H */
