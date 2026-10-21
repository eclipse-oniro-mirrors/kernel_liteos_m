/*
 * Copyright (c) 2022-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * fuzz_common.c -- DTFuzz-compatible mutation engine.
 *
 * Each DT_SetGet* fills the element's buffer with pseudo-random bytes from
 * rand() and returns a pointer to it.  The caller casts/dereferences as the
 * parameter type (mirroring the original DTFuzz usage pattern).  The `def`
 * argument biases the very first call; rand() provides all later values.
 *
 * The PRNG is seeded with a fixed value so every run replays the same
 * mutation sequence (reproducibility).
 */

#include "fuzz_common.h"
#include <stdlib.h>
#include <string.h>
#include "los_debug.h"
#include "los_task_pri.h"
#include "los_sem.h"
#include "los_mux.h"
#include "los_queue.h"
#include "los_swtmr_pri.h"

/* 内核资源池数组（testsuites 既有惯例：osTest.c 同款 extern） */
extern LosTaskCB *g_osTaskCBArray;
extern LosSemCB *g_allSem;
extern LosQueueCB *g_allQueue;
extern LosMuxCB *g_allMux;
extern SWTMR_CTRL_S *g_osSwtmrCBArray;

S_ElementInit g_element[ELEMENT_LEN];
int g_iteration = 0;

/* ---- 已有资源保护：快照 + 守卫 ---- */

static UINT8 g_taskBusy[LOSCFG_BASE_CORE_TSK_LIMIT];
static UINT8 g_semBusy[LOSCFG_BASE_IPC_SEM_LIMIT];
static UINT8 g_queueBusy[LOSCFG_BASE_IPC_QUEUE_LIMIT];
static UINT8 g_muxBusy[LOSCFG_BASE_IPC_MUX_LIMIT];
static UINT8 g_swtmrBusy[LOSCFG_BASE_CORE_SWTMR_LIMIT];
static UINT32 g_guardReady = 0;
static UINT32 g_guardRedirects = 0;   /* 拦截计数：变异值命中已存在资源被重定向的次数 */

void FuzzGuardSnapshot(void)
{
    UINT32 i;

    for (i = 0; i < LOSCFG_BASE_CORE_TSK_LIMIT; i++) {
        g_taskBusy[i] = (g_osTaskCBArray[i].taskStatus & OS_TASK_STATUS_UNUSED) ? 0 : 1;
    }
    for (i = 0; i < LOSCFG_BASE_IPC_SEM_LIMIT; i++) {
        g_semBusy[i] = (((LosSemCB *)g_allSem)[i].semStat == OS_SEM_UNUSED) ? 0 : 1;
    }
    for (i = 0; i < LOSCFG_BASE_IPC_QUEUE_LIMIT; i++) {
        g_queueBusy[i] = (((LosQueueCB *)g_allQueue)[i].queueState == OS_QUEUE_INUSED) ? 0 : 1;
    }
    for (i = 0; i < LOSCFG_BASE_IPC_MUX_LIMIT; i++) {
        g_muxBusy[i] = (((LosMuxCB *)g_allMux)[i].muxStat == OS_MUX_USED) ? 1 : 0;
    }
    for (i = 0; i < LOSCFG_BASE_CORE_SWTMR_LIMIT; i++) {
        g_swtmrBusy[i] = (((SWTMR_CTRL_S *)g_osSwtmrCBArray)[i].state != OS_SWTMR_STATUS_UNUSED) ? 1 : 0;
    }
    g_guardReady = 1;
}

/* 守卫通用逻辑：变异值命中"快照中已存在资源"则重定向为 ownId；
 * ownId 无效(0xFFFFFFFF)时重定向为必然越界的值（仅打错误分支）。
 * 越界变异值原样保留（安全，走 invalid-arg 错误分支）。 */
static UINT32 FuzzGuardCommon(UINT32 mutated, UINT32 ownId, UINT32 limit, const UINT8 *busy)
{
    if (!g_guardReady || mutated >= limit) {
        return mutated;
    }
    if (busy[mutated] && mutated != ownId) {
        g_guardRedirects++;
        return (ownId < limit) ? ownId : (limit + 0x10000);   /* own 无效则给越界值 */
    }
    return mutated;
}

UINT32 FuzzGuardRedirectCount(void)
{
    return g_guardRedirects;
}

UINT32 FuzzGuardTaskId(UINT32 mutated, UINT32 ownId)
{
    return FuzzGuardCommon(mutated, ownId, LOSCFG_BASE_CORE_TSK_LIMIT, g_taskBusy);
}

UINT32 FuzzGuardSemId(UINT32 mutated, UINT32 ownId)
{
    return FuzzGuardCommon(mutated, ownId, LOSCFG_BASE_IPC_SEM_LIMIT, g_semBusy);
}

UINT32 FuzzGuardQueueId(UINT32 mutated, UINT32 ownId)
{
    return FuzzGuardCommon(mutated, ownId, LOSCFG_BASE_IPC_QUEUE_LIMIT, g_queueBusy);
}

UINT32 FuzzGuardMuxId(UINT32 mutated, UINT32 ownId)
{
    return FuzzGuardCommon(mutated, ownId, LOSCFG_BASE_IPC_MUX_LIMIT, g_muxBusy);
}

UINT32 FuzzGuardSwtmrId(UINT32 mutated, UINT32 ownId)
{
    return FuzzGuardCommon(mutated, ownId, LOSCFG_BASE_CORE_SWTMR_LIMIT, g_swtmrBusy);
}

/* ---- lifecycle ---- */

void INIT_FuzzEnvironment(void)
{
    static int seeded = 0;
    if (!seeded) {
        srand(0xC0FFEE);
        seeded = 1;
    }
    memset(g_element, 0, sizeof(g_element));
    g_iteration = 0;
    /* 快照系统已占用资源：本用例的变异从此不触碰它们 */
    FuzzGuardSnapshot();
}

void CLEAR_FuzzEnvironment(void)
{
    /* nothing to tear down */
}

void DT_Clear(S_ElementInit *element)
{
    if (element != NULL) {
        memset(element, 0, sizeof(S_ElementInit));
    }
}

/* ---- internal: fill n random bytes into element->buf ---- */

static void fill_random(S_ElementInit *e, size_t n)
{
    size_t i;
    if (n > sizeof(e->buf)) {
        n = sizeof(e->buf);
    }
    for (i = 0; i < n; i++) {
        e->buf[i] = (UINT8)(rand() & 0xFF);
    }
    e->curLen = (UINT32)n;
}

/* ---- random-input generators ----
 * Value-domain biased mutation (borrowed from syzkaller's value biasing and
 * FreeRTOS fuzzing's resource-pool injection): a uniformly random 32-bit
 * value almost never lands inside a kernel handle/priority/size valid range
 * (e.g. sem handles 0..LOSCFG_BASE_IPC_SEM_LIMIT), so most iterations only
 * exercise the invalid-argument error branch.  Distribution below:
 *   60% small values (0..63)  -- handles, prios, small sizes/counters
 *   20% boundary values       -- 2^n-1 edges, UINT16/32 max, sign bit
 *   20% fully random          -- keep exploring the whole space
 */
static UINT32 biased_u32(void)
{
    static const UINT32 bounds[] = {
        0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256,
        0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF,
    };
    UINT32 sel = (UINT32)rand() % 100;
    if (sel < 60) {
        return (UINT32)rand() % 64;
    }
    if (sel < 80) {
        return bounds[(UINT32)rand() % (sizeof(bounds) / sizeof(bounds[0]))];
    }
    return ((UINT32)rand() << 16) ^ (UINT32)rand();
}

static void fill_biased(S_ElementInit *e, size_t n)
{
    UINT32 v;
    if (n > sizeof(e->buf)) {
        n = sizeof(e->buf);
    }
    v = biased_u32();
    if (n > sizeof(UINT32)) {
        n = sizeof(UINT32);   /* scalar generators never need more than 4 */
    }
    memcpy(e->buf, &v, n);
    e->curLen = (UINT32)n;
}

void *DT_SetGetS32(S_ElementInit *element, int def)
{
    if (element->curLen == 0) {
        /* first call: seed with the default value */
        memcpy(element->buf, &def, sizeof(int));
        element->curLen = sizeof(int);
    } else {
        fill_biased(element, sizeof(int));
    }
    return (void *)element->buf;
}

void *DT_SetGetU8(S_ElementInit *element, int def)
{
    if (element->curLen == 0) {
        element->buf[0] = (UINT8)def;
        element->curLen = 1;
    } else {
        fill_biased(element, 1);
    }
    return (void *)element->buf;
}

void *DT_SetGetU16(S_ElementInit *element, int def)
{
    if (element->curLen == 0) {
        UINT16 v = (UINT16)def;
        memcpy(element->buf, &v, sizeof(UINT16));
        element->curLen = sizeof(UINT16);
    } else {
        fill_biased(element, sizeof(UINT16));
    }
    return (void *)element->buf;
}

void *DT_SetGetU32(S_ElementInit *element, int def)
{
    if (element->curLen == 0) {
        UINT32 v = (UINT32)def;
        memcpy(element->buf, &v, sizeof(UINT32));
        element->curLen = sizeof(UINT32);
    } else {
        fill_biased(element, sizeof(UINT32));
    }
    return (void *)element->buf;
}

int64_t DT_SetGetS64(S_ElementInit *element, int64_t def)
{
    if (element->curLen == 0) {
        memcpy(element->buf, &def, sizeof(int64_t));
        element->curLen = sizeof(int64_t);
    } else {
        fill_random(element, sizeof(int64_t));
    }
    return *(int64_t *)element->buf;
}

/* ---- helpers ---- */

void FuzzHeartbeat(int iter)
{
    if (iter % FZ_HEARTBEAT_EVERY == 0) {
        PRINTK("[FUZZ] iter %d\n", iter);
    }
}

void FuzzWatchdogFeed(void)
{
    /* no-op on qemu; ws63 watchdog is fed by the system tick */
}
