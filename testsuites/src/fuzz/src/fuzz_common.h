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

#ifndef _FUZZ_COMMON_H
#define _FUZZ_COMMON_H

#include "los_typedef.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * fuzz_common.h -- shared infrastructure for all fuzz test cases.
 *
 * Pattern (per test case, one file per interface):
 *   1. Loop CYCLE_TOTAL_TIMES iterations
 *   2. Each iteration: fill g_element[idx] with pseudo-random bytes
 *      via DT_SetGet*, cast/deref as the parameter type
 *   3. Call the target API with the mutated parameter
 *   4. If the API allocated a resource, free it in the same iteration
 *   5. Never pass excessive timeouts (mask with % SMALL_VALUE)
 *
 * The PRNG is seeded once with a fixed value (reproducible runs).
 */

#define CYCLE_TOTAL_TIMES  128   /* iterations per test case (baseline) */
/* Heavy-tier cycle count: cases whose per-iteration cost is dominated by task
 * creation/teardown (~1.5ms) or sleeping APIs run this many iterations in soak
 * builds, keeping the whole suite fast while preserving exploration. */
/* tiering is only active in soak builds (CYCLE_TOTAL_TIMES raised far above
 * the 128 baseline); at baseline every case runs the same CYCLE_TOTAL_TIMES
 * so the full suite stays a ~20s regression run */
#if CYCLE_TOTAL_TIMES > 10000
/* heavy-tier: per-iteration cost dominated by task/thread create+teardown
 * (stack alloc/memset ~1.5ms) */
#define CYCLE_HEAVY_TIMES  100000
/* sleep-tier: per-iteration cost dominated by a fixed sleep (tick-rounded to
 * ~10ms at the default 100Hz tick) */
#define CYCLE_SLEEP_TIMES  10000
#else
#define CYCLE_HEAVY_TIMES  CYCLE_TOTAL_TIMES
#define CYCLE_SLEEP_TIMES  CYCLE_TOTAL_TIMES
#endif
/* heartbeat print period: UART printing is the dominant soak-run cost */
#define FZ_HEARTBEAT_EVERY 65536
#define ELEMENT_LEN        16    /* g_element pool size */
#define FUZZ_BUF_SIZE      64    /* per-element scratch buffer */

/* one element of the mutation pool */
typedef struct {
    UINT8  buf[FUZZ_BUF_SIZE];
    UINT32 curLen;
} S_ElementInit;

/* global mutation pool (one per test process) */
extern S_ElementInit g_element[ELEMENT_LEN];
extern int g_iteration;

/* ---- lifecycle (no-ops in this implementation, kept for compatibility) ---- */
void INIT_FuzzEnvironment(void);
void CLEAR_FuzzEnvironment(void);
void DT_Clear(S_ElementInit *element);

/* ---- random-input generators ----
 * Each fills g_element[idx] with pseudo-random bytes and returns a pointer
 * to it.  The `def` argument biases the first call but rand() provides
 * all subsequent values. */
void   *DT_SetGetS32(S_ElementInit *element, int def);
void   *DT_SetGetU8(S_ElementInit *element, int def);
void   *DT_SetGetU16(S_ElementInit *element, int def);
void   *DT_SetGetU32(S_ElementInit *element, int def);
int64_t DT_SetGetS64(S_ElementInit *element, int64_t def);

/* ---- helpers ---- */
void FuzzHeartbeat(int iter);
void FuzzWatchdogFeed(void);

/* ---- 已有资源保护（问题 40 教训的通用化） ----
 * 每条用例 INIT_FuzzEnvironment 时快照系统已占用的 任务/信号量/队列/
 * 互斥锁/软件定时器；变异值命中"快照中已存在的资源"时重定向为自建
 * 资源 ID（ownId），越界值原样保留（打错误分支）。保证 fuzz 只可能
 * 销毁自己创建的资源，绝不触碰 fuzz 之前系统已创建的资源。
 *
 * 三类接口的资源池收敛关系（同一查询方法即全覆盖，无需分层查询）：
 *   CMSIS:  osSemaphoreNew->LOS_SemCreate, osMessageQueueNew->LOS_QueueCreate,
 *           osMutexNew->LOS_MuxCreate, osThreadNew->任务池
 *   POSIX:  sem_init->LOS_SemCreate, mq_open->LOS_QueueCreate,
 *           pthread_mutex_init->LOS_MuxCreate, pthread_create->任务池
 *   即 POSIX/CMSIS 创建的资源全部落在 LOS 池中，被本快照统一保护；
 *   跨层混用（如 LOS_SemDelete 删掉 CMSIS 创建的信号量）同样被拦截。
 *
 * 用例编写规范：
 *   0. 资源申请成功后必须在同一轮次内完成清理（in-iteration cleanup）：
 *      每轮 create/New/init 成功的资源，在该轮结束前必须 delete/Destroy/
 *      free/join 兜底回收——禁止跨轮持有（删旧建新模式）与循环尾补删。
 *      违反会表现为 ResLeak 跨用例漂移或 ZOMBIE 累积（问题 40 关联）。
 *
 * 防接口混用：
 *   1. CMSIS 的 id 是指针（LosSemCB* 等，池地址域），不是索引——禁止把
 *      索引域变异值直接当 CMSIS id 传参；CMSIS 野 id 探测走 bug_isolate
 *      隔离探针（如 S01），不进基线变异循环。
 *   2. POSIX 对象（pthread_mutex_t/sem_t 等）为结构体，只传自建对象，
 *      不变异其内部 handle 字段。
 *   3. HWI 无统一查询接口，中断号沿用静态安全号段（见 hwi 用例注释）。
 *   4. 每条用例只操作一层 API 创建的资源，禁止跨层操作。 */
void FuzzGuardSnapshot(void);
UINT32 FuzzGuardRedirectCount(void);
UINT32 FuzzGuardTaskId(UINT32 mutated, UINT32 ownId);
UINT32 FuzzGuardSemId(UINT32 mutated, UINT32 ownId);
UINT32 FuzzGuardQueueId(UINT32 mutated, UINT32 ownId);
UINT32 FuzzGuardMuxId(UINT32 mutated, UINT32 ownId);
UINT32 FuzzGuardSwtmrId(UINT32 mutated, UINT32 ownId);

#ifdef __cplusplus
}
#endif

#endif /* _FUZZ_COMMON_H */
