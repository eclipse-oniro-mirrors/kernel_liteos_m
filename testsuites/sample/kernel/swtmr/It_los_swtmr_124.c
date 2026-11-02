/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the conditions of the LOSCFG-style open source license.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 */

/* 用例简要描述: 【BUG_23 缺陷暴露用例】同 tick 到期数量超过 expiryList 容量时 ONCE 定时器槽位泄漏
 *
 * 【现象】    同一 tick 内到期的定时器数超过 expiryList 容量(=LOSCFG_BASE_CORE_
 *            SWTMR_LIMIT)时, OsSwtmrScan 对溢出定时器不入队(inProcess 不增),
 *            但 OsSwtmrUpdate 仍将其置 OS_SWTMR_STATUS_DELETING(ONCE 型);
 *            自删回收仅发生在 swtmr 任务处理已入队定时器时
 *            (OsSwtmrCheckSelfDelete), 未入队者永远触达不到; 而
 *            LOS_SwtmrDelete 对 DELETING 态返回 LOS_ERRNO_SWTMR_NOT_CREATED
 *            (非 SYNC_DELETE 配置) -> 槽位永久无法归还 free list。
 *
 *            构造(容量 N=48, 系统已占用 S 时配额 B=N-S-2):
 *            B 族(ONCE, 10 tick) + C(PERIODIC, 10 tick) 在 T+10 首批到期入队,
 *            首个被处理条目的回调在 swtmr 任务内按 tick 计数忙等 215 tick
 *            (系统任务不可阻塞: sem/mux/event/delay 均被拒绝, 只能忙等;
 *            tick ISR 照常触发, C 每 10 tick 周期重排持续入队) ->
 *            环形队列在 T+30 前必然打满(free==0)并保持;
 *            A(ONCE, 200 tick) 在 T+200 到期时 free==0 被丢弃,
 *            置 DELETING 后永久滞留 -> 定时器池净泄漏 1 个槽位。
 * 【根因】    kernel/base/los_swtmr.c:410-418 OsSwtmrScan 的
 *            `if (expiryList.free != 0)` 入队失败分支无任何补偿(不计数不置
 *            待处理标志), :219 OsSwtmrUpdate 对 ONCE 无条件置 DELETING,
 *            两者组合使被丢弃的 ONCE 定时器进入无人回收的 DELETING 终态。
 * 【修复方向】入队失败分支对 ONCE 定时器直接同步执行 OsSwtmrDelete(或至少
 *            保持 CREATED 状态允许用户补删), 不应置 DELETING 后丢弃。
 * 【恢复方法】修复后把 bug_isolate.h 中 KERNEL_BUG_23_SWTMR_EXPIRYLIST_LEAK 置 1,
 *            聚合注册点(It_los_swtmr.c)恢复调用, 用例恢复执行作为回归看护常跑。
 * 【关联】    BUG_23 / resleak 批次问题记录 20261023_213007。
 * 修正记录:  ①未计系统占用槽位; ②慢回调 LOS_TaskDelay 在系统任务被拒(:1677);
 *            ③LOS_SwtmrCreate 禁止 handler==NULL(:531), A/探测传 NULL 必然
 *            创建失败(三轮假绿根因); ④计数断言被系统定时器自然消亡的基线
 *            漂移抵消, 改读 A 控制块(timerId 代数 + state==DELETING)判定;
 *            ⑤215 tick 长忙等触发 tickless 时间片响应模式(g_schedResponseID,
 *            los_sched_tick.c:202/423)冻结 tick 内 swtmr 扫描; ⑥最终定位:
 *            qemu 板级配置 LOSCFG_BASE_CORE_SWTMR_IN_ISR=1(target_config.h:69),
 *            回调在 tick ISR 内联执行, expiryList 环形队列分支整体编译出局
 *            (镜像无 OsSwtmrTask, expiryList.free 为 BSS 零假象) —— 本缺陷
 *            在 qemu 结构性不可达, 仅 swtmr 任务形态板(周期 tick, 如 ws63)
 *            可触发; 注册点加板级外层条件, qemu 不注册不执行。
 *            环形形态板上的构造: B 族(ONCE,10tick)到期入队 + 首条目回调
 *            tick 计数忙等 10 tick 压住排水 + C(PERIODIC,1tick)每 tick 补格
 *            打满 + A(ONCE,16tick)到期必遇 free==0 被丢弃。 */
#include "It_los_swtmr.h"
#include "los_swtmr.h"
#include "los_swtmr_pri.h"
#include "los_task.h"
#include "los_tick.h"
#include "los_percpu_pri.h"

extern UINT32 SwtmrCountGetTest(VOID);
extern SWTMR_CTRL_S *g_osSwtmrCBArray;

#define SWTMR124_INTERVAL_B   10  /* B 族到期 tick T0+10 */
#define SWTMR124_INTERVAL_C   1   /* C 每 tick 到期, 逐格填充环形队列 */
#define SWTMR124_INTERVAL_A   16  /* A 于 T0+16 到期, 此时环形队列已被 C 填满 */
#define SWTMR124_SPIN_TICKS   10  /* 首条目回调忙等 10 tick: 压住排水且不触发
                                   * tickless 时间片响应冻结(实测约 30 tick 后介入) */
#define SWTMR124_WAIT_TICKS   30
#define SWTMR124_SPIN_MAX     1000000

static volatile UINT32 g_swtmr124SpinDone = 0;
static volatile UINT32 g_swtmr124InvokeCnt = 0;
static volatile UINT32 g_swtmr124SpinStart = 0;
static volatile UINT32 g_swtmr124SpinEnd = 0;
static volatile UINT32 g_swtmr124TraceTick[64]; /* 64: 采样上限 */
static volatile UINT32 g_swtmr124TraceFree[64]; /* 64: 采样上限 */
static volatile UINT32 g_swtmr124TraceN = 0;

static VOID Swtmr124OnceHandler(UINTPTR arg)
{
    UINT32 tickBegin;

    (VOID)arg;
    g_swtmr124InvokeCnt++;
    if (g_swtmr124TraceN < 64) { /* 64: 采样上限 */
        g_swtmr124TraceTick[g_swtmr124TraceN] = LOS_TickCountGet();
        g_swtmr124TraceFree[g_swtmr124TraceN] = OsPercpuGet()->expiryList.free * 16 + 1; /* 16: marker 打包, 1=B */
        g_swtmr124TraceN++;
    }
    /* 一次性忙等: 首个被处理的到期条目在 swtmr 任务(最高优先级, 系统任务
     * 不可阻塞)内按 tick 计数忙等 215 tick, 压住环形队列只进不出;
     * 忙等期间 tick ISR 照常入队(C 周期重排), A 的 T+200 到期必遇 free==0。
     * 后续条目立即返回快速排空 */
    if (g_swtmr124SpinDone == 0) {
        g_swtmr124SpinDone = 1;
        tickBegin = LOS_TickCountGet();
        g_swtmr124SpinStart = tickBegin;
        while ((LOS_TickCountGet() - tickBegin) < SWTMR124_SPIN_TICKS) {
        }
        g_swtmr124SpinEnd = LOS_TickCountGet();
    }
}

static VOID Swtmr124NoopHandler(UINTPTR arg)
{
    (VOID)arg;
}

static VOID Swtmr124RecordHandler(UINTPTR arg)
{
    /* 记录型回调: 仅采样 (tick, free, marker), marker=2(A)/3(C) 由 arg 传入 */
    if (g_swtmr124TraceN < 64) { /* 64: 采样上限 */
        g_swtmr124TraceTick[g_swtmr124TraceN] = LOS_TickCountGet();
        g_swtmr124TraceFree[g_swtmr124TraceN] = OsPercpuGet()->expiryList.free * 16 + (UINT32)arg; /* 16: marker 打包进低 4 位 */
        g_swtmr124TraceN++;
    }
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 i;
    UINT32 tickStart;
    UINT32 spin = 0;
    UINT32 cntBefore;
    UINT32 nFree = 0;
    UINT32 cntB;
    UINT32 createdB = 0;
    UINT32 aId = 0xFFFFFFFF;
    UINT32 cId = 0xFFFFFFFF;
    UINT32 *bIds = NULL;
    UINT32 cntAfter;
    UINT32 delRetA;
    UINT32 delRetC;
    static UINT32 bIdBuf[LOSCFG_BASE_CORE_SWTMR_LIMIT];
    static UINT32 probeIds[LOSCFG_BASE_CORE_SWTMR_LIMIT];

    cntBefore = SwtmrCountGetTest();
    g_swtmr124SpinDone = 0;

    /* 探测真实可用槽数: 创建不启动的 ONCE 定时器直到失败, 再全部删除归还
     * (SwtmrCountGetTest 与实际 free list 存在差量: 系统占用不可见部分,
     * 实测 count=1 时 A 仍创建失败, 故以探测值为准) */
    for (i = 0; i < LOSCFG_BASE_CORE_SWTMR_LIMIT; i++) {
        ret = LOS_SwtmrCreate(1000, LOS_SWTMR_MODE_ONCE, Swtmr124NoopHandler, &probeIds[i], 0); /* 1000: 不启动不会到期 */
        if (ret != LOS_OK) {
            break;
        }
        nFree++;
    }
    for (i = 0; i < nFree; i++) {
        (VOID)LOS_SwtmrDelete(probeIds[i]);
    }
    dprintf("[Swtmr124] count=%u probeFree=%u\n", cntBefore, nFree);

    /* 真实配额: B = 探测可用数 - 3 (预留 C 与 A, 1 个余量) */
    if ((nFree < 8) || (nFree > LOSCFG_BASE_CORE_SWTMR_LIMIT)) {
        dprintf("[Swtmr124] probeFree=%u out of range, skip\n", nFree);
        return LOS_OK;
    }
    cntB = nFree - 3;
    bIds = bIdBuf;

    /* 对齐到新 tick 边界: 保证全部创建+启动落在同一 tick 内(到期 tick 一致) */
    tickStart = LOS_TickCountGet();
    while ((LOS_TickCountGet() == tickStart) && (spin < SWTMR124_SPIN_MAX)) {
        spin++;
    }

    /* B 族: ONCE 型 10 tick 到期, 统一挂一次性忙等回调 */
    for (i = 0; i < cntB; i++) {
        ret = LOS_SwtmrCreate(SWTMR124_INTERVAL_B, LOS_SWTMR_MODE_ONCE, Swtmr124OnceHandler, &bIds[i], 0);
        if (ret != LOS_OK) {
            dprintf("[Swtmr124] B[%u] create fail ret=0x%x\n", i, ret);
            goto CLEANUP;
        }
        createdB++;
        ret = LOS_SwtmrStart(bIds[i]);
        if (ret != LOS_OK) {
            dprintf("[Swtmr124] B[%u] start fail ret=0x%x\n", i, ret);
            goto CLEANUP;
        }
    }
    /* C: 周期型 1 tick, 每 tick 补一格, 在 A 到期(T0+16)前必然打满环形队列
     * (忙等期间 tick 扫描正常, C 链持续供给; 打满后 C 溢出丢弃无害-周期型自重排) */
    ret = LOS_SwtmrCreate(SWTMR124_INTERVAL_C, LOS_SWTMR_MODE_PERIOD, Swtmr124RecordHandler, &cId, 3); /* 3: C 标记 */
    if (ret != LOS_OK) {
        dprintf("[Swtmr124] C create fail ret=0x%x\n", ret);
        goto CLEANUP;
    }
    ret = LOS_SwtmrStart(cId);
    if (ret != LOS_OK) {
        dprintf("[Swtmr124] C start fail ret=0x%x\n", ret);
        goto CLEANUP;
    }
    /* A: ONCE 型 16 tick, 到期时环形队列已满(free==0) -> 被丢弃并滞留 DELETING */
    ret = LOS_SwtmrCreate(SWTMR124_INTERVAL_A, LOS_SWTMR_MODE_ONCE, Swtmr124RecordHandler, &aId, 2); /* 2: A 标记 */
    if (ret != LOS_OK) {
        dprintf("[Swtmr124] A create fail ret=0x%x\n", ret);
        goto CLEANUP;
    }
    ret = LOS_SwtmrStart(aId);
    if (ret != LOS_OK) {
        dprintf("[Swtmr124] A start fail ret=0x%x\n", ret);
        goto CLEANUP;
    }

    /* 等待: T0+16 A 被丢弃, T0+20 忙等结束排空 B 族自删后本任务唤醒 */
    (VOID)LOS_TaskDelay(SWTMR124_WAIT_TICKS);

    {
        SWTMR_CTRL_S *acb = g_osSwtmrCBArray + (aId % LOSCFG_BASE_CORE_SWTMR_LIMIT);
        SWTMR_CTRL_S *ccb = g_osSwtmrCBArray + (cId % LOSCFG_BASE_CORE_SWTMR_LIMIT);
        dprintf("[Swtmr124] pre-del: Astate=%u AidMatch=%d Cstate=%u Coverrun=%u\n",
                acb->state, (acb->timerId == aId) ? 1 : 0, ccb->state, ccb->overrun);
    }

    delRetA = LOS_SwtmrDelete(aId);
    dprintf("[Swtmr124] delete(A) ret=0x%x (unfixed expect NOT_CREATED 0x%x), handlerInv=%u spin=[%u,%u]\n",
            delRetA, LOS_ERRNO_SWTMR_NOT_CREATED, g_swtmr124InvokeCnt,
            g_swtmr124SpinStart, g_swtmr124SpinEnd);

CLEANUP:
    /* B 族为 ONCE 型, 正常路径已自删; 失败路径残留者在此兜底删除(已删返回错误忽略) */
    for (i = 0; i < createdB; i++) {
        (VOID)LOS_SwtmrDelete(bIds[i]);
    }
    /* C 周期型仍在运行: 停止并删除(正常路径, 应成功) */
    delRetC = LOS_SwtmrDelete(cId);
    dprintf("[Swtmr124] delete(C) ret=0x%x (expect 0)\n", delRetC);

    cntAfter = SwtmrCountGetTest();
    dprintf("[Swtmr124] swtmrCount before=%u after=%u (drift-tolerant, primary assert below)\n",
            cntBefore, cntAfter);

    /* 直接读 A 的控制块判定滞留: 槽位代数未更新(timerId==aId)且状态为
     * DELETING = 被丢弃后无人回收的终态(与系统定时器自然消亡的基线漂移免疫) */
    {
        SWTMR_CTRL_S *acb = g_osSwtmrCBArray + (aId % LOSCFG_BASE_CORE_SWTMR_LIMIT);
        UINT32 stuck = ((acb->timerId == aId) && (acb->state == OS_SWTMR_STATUS_DELETING)) ? 1 : 0;
        dprintf("[Swtmr124] A slot: timerId=0x%x aId=0x%x state=%u (DELETING=%d)\n",
                acb->timerId, aId, acb->state, OS_SWTMR_STATUS_DELETING);

        /* 修复后契约: 被溢出丢弃的 ONCE 定时器不得滞留 DELETING 终态 */
        ICUNIT_ASSERT_EQUAL(stuck, 0, acb->state);
        for (i = 0; i < g_swtmr124TraceN; i++) { /* 诊断: 回调视角的环形队列动态(tick, free, mark) */
            dprintf("[Swtmr124] trace[%u] tick=%u free=%u mark=%u\n", i, g_swtmr124TraceTick[i],
                    g_swtmr124TraceFree[i] / 16, g_swtmr124TraceFree[i] % 16); /* 16: marker 打包系数 */
        }
    }

    return LOS_OK;
}

VOID ItLosSwtmr124(VOID)
{
    TEST_ADD_CASE("ItLosSwtmr124", Testcase, TEST_LOS, TEST_SWTMR, TEST_LEVEL0, TEST_FUNCTION);
}
