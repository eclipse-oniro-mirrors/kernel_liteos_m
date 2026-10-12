/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * 产品 BUG 用例隔离宏(全局唯一定义点)。
 *
 * ==================== 机制(20260923 统一) ====================
 * 发现商用代码 BUG 时, 暴露缺陷的用例按"注册点隔离"法接入本文件:
 *   1. 宏统一定义于此(命名 <层>_BUG_<编号>_<摘要>, 层 ∈ KERNEL/POSIX/
 *      CMSIS/FUZZ, 编号沿用隔离清单 BUG_NN, 不复用已消解编号);
 *   2. 隔离点:
 *      a) 独立用例文件(既有或新增, 如 It_posix_signal_094.c、it_*_fuzz.c、
 *         It_los_lms_039/040.c、It_los_tick_014.c): 用例文件保持普通形态
 *         (不裹任何宏, TestCase/注册函数无条件编译), 在聚合/套件注册点
 *         #if 包 ItXxx() 调用 —— 隔离态不调用即不注册不执行;
 *      b) 套件内嵌用例(LITE_TEST_CASE + ADD_TEST_CASE 同文件, 如
 *         cmsis_func_test.c): 定义保持普通形态, 仅守卫注册点
 *         (ADD_TEST_CASE);
 *   3. 守卫统一使用严格形式且必须单独一行: #if defined(<宏>) && <宏> == 1
 *      (仅精确 1 才执行: 未定义/拼写错/误设 2 等一律保持隔离,
 *       与 LOSCFG_XXX == 1 既有风格一致);
 *      多条件场景(如架构限定): 其它条件放外层, BUG 宏守卫单独一行放最里层,
 *      禁止与其它条件合并为一个 #if 表达式;
 *   4. 用例断言必须写"修复后契约"(未修复=红灯), 修复后置 1 即通过;
 *   5. 本文件由 osTest.h 顶层统一 #include(自顶向下), 全部测试翻译单元
 *      自动可见 —— 任何文件不得再散落 #include "bug_isolate.h"
 *      (散落引用曾致聚合守卫读不到宏定义、置 1 无法恢复的隐患)。
 *
 * 宏开关语义(全部一致):
 *   0 = 缺陷未修复: 用例不注册不执行, 基线保持全绿;
 *   1 = 缺陷已修复: 用例恢复注册执行, 作为回归看护用例常跑。
 *
 * 当前隔离清单(缺陷编号见 总览.md 附录 B):
 *   - ItLosTestCmsisKernel008 (BUG_01: osKernelInitialize guard 失效,
 *     仅 qemu/ARM, cmsis_func_test.c:1449 附近)
 *   - ItPosixPipe003          (BUG_02: poll-on-pipe ws63 复位/qemu sem 竞态;
 *     qemu/arm 已验证正常
 *   - ItPosixSignal091        (BUG_03: sigtimedwait 后续 semaphore 误 post,
 *     疑与 BUG_08 同根因; BUG_08 已修复(20260923)恢复 sigwait 族阻塞唤醒路径,
 *     本项保留隔离待复测 semaphore 误 post 是否随同消解)
 *
 * 历史隔离项(已消解):
 *   - ItPosixSignal094        (BUG_08: posix sigwait 族唤醒 mask 位序错位 ——
 *     musl porting sigaddset 用 1U<<sig, 内核 LOS_SIGNAL_MASK 用 1U<<(sigNo-1),
 *     SignalSend 唤醒判断永不命中 -> sigwait/sigwaitinfo/sigtimedwait 等待者
 *     永睡; qemu 内核探针实证 waitFlag=0x1000 vs LOS_SIGNAL_MASK(12)=0x800;
 *     20260923 已修复: porting sigaddset/sigdelset/sigismember 统一为 1U<<(sig-1),
 *     lib signal.c 补 sigwait 族成功返回信号号/真阻塞语义; 用例已恢复执行看护)
 *   - ItPosixPthread034 (BUG_07 布局敏感) —— 经接口定性复审, __sched_cpucount
 *     为 musl 实现内部符号(双下划线保留命名空间,非 POSIX 对外接口,posix 串讲
 *     G2 分类"内部辅助"),本就不应直测;用例降级为不直测并从 BUILD.gn 移除,
 *     BUG_07 无需布局修复即消解。其可测行为由 pthread_032/033(affinity 族
 *     对外接口)间接覆盖。
 *   - 历史机制 PRODUCT_BUG_ISOLATE(单宏全量隔离)已退役, 全部条目迁移至
 *     下方每用例宏(20260922)。
 */

#ifndef _BUG_ISOLATE_H
#define _BUG_ISOLATE_H

/*
 * ==================== 隔离用例 BUG 注释规范 ====================
 * 被隔离的用例（宏=0 期间）是正常的测试用例源文件，按正常用例命名
 * （文件名 It_los_<模块>_<编号>.c / it_<接口描述>_fuzz.c，用例函数名
 * It<模块><接口>），仅在 bug_isolate.h 留一个宏开关。
 * 用例文件头必须按以下字段写注释（字段顺序固定，全部必填除【关联】）：
 *
 *   【现象】    触发条件 + 观察到的错误行为（崩溃类型/错误码/复现单板）
 *   【根因】    缺陷代码位置(文件:函数) + 出错机理（缺失的校验/错误的逻辑）
 *   【修复方向】期望的正确行为（应返回的错误码/应增加的校验）
 *   【恢复方法】修复后把 bug_isolate.h 中对应宏置 1，用例恢复注册执行，
 *               作为回归看护用例常跑
 *   【关联】    (可选) 缺陷单号 / 同源隔离项
 *
 * 宏开关语义：
 *   0 = 缺陷未修复: 用例不注册不执行, 基线保持全绿
 *   1 = 缺陷已修复: 用例恢复执行, 作为回归看护常跑
 * ==============================================================
 */

/* ==================== 功能用例隔离宏(每用例一个) ==================== */
#define CMSIS_BUG_01_KERNELINIT_GUARD   1 /* ItLosTestCmsisKernel008: osKernelInitialize Running
                                           * 态重入 guard 失效(BUG_01, 仅 qemu/ARM; 与 FUZZ_BUG_S11 同源)。
                                           * 隔离点在 cmsis_func_test.c 注册处: 外层 !LOSCFG_ARCH_ARM,
                                           * 内层本宏守卫单独一行(纯两层嵌套), 非 ARM 修复后置 1 恢复 */
#define POSIX_BUG_02_PIPE_POLL          0 /* ItPosixPipe003: poll-on-pipe ws63 挂死复位
                                           * (BUG_02, 疑似 PipePoll 内部函数缺陷) */
#define POSIX_BUG_03_SIGTIMEDWAIT       0 /* ItPosixSignal091: sigtimedwait 后续 semaphore 误
                                           * post(BUG_03, 疑与 BUG_08 同根因) */
#define POSIX_BUG_08_SIGWAIT_MASK       0 /* ItPosixSignal094: sigwait 族唤醒 mask 位序错位
                                           * (BUG_08, musl/内核位序不一致, 等待者永睡) */
#define KERNEL_BUG_09_LMS_ERRINFO_NULL  0 /* ItLosLms039: LMS OsLmsGetErrorInfo 池外报告地址
                                           * 空指针解引用(BUG_09, 问题 #50); 隔离点在
                                           * It_los_lms.c 聚合注册点, 置 1 恢复调用 */
#define KERNEL_BUG_10_LMS_ADDPOOL_DEADNODE 0 /* ItLosLms040: LMS LOS_LmsAddCheckPool 小池 realSize
                                               * 归零仍注册 poolSize=0 死节点占用池表槽位
                                               * (BUG_10, 问题 #51); 用例断言修复后契约,
                                               * 修复前置 1 将失败; 隔离点在 It_los_lms.c
                                               * 聚合注册点 */
#define KERNEL_BUG_11_PM_TICKTIMER_ASSERT  0 /* ItLosPm005: qemu(TICKLESS_GLOBAL)下 OsPmTickTimerStop
                                               * 的 LOS_ASSERT(expireTime > currTime) 时序竞态挂死
                                               * (BUG_11): 最近到期点落进关中断窗口即触发
                                               * (欠账 1~2K cycles), 全量+双板并行负载 9/11 命中,
                                               * 单跑相位敏感不现; 隔离点在 It_los_pm.c 聚合注册点,
                                               * 仅 tickless 板隔离, ws63(周期 tick)构造上不可触发
                                               * 照常执行; 修复后置 1 恢复 */
#define KERNEL_BUG_12_PM_TICKTIMER_PERIODIC_DEAD 0 /* ItLosPm006: 周期 tick 板(ws63) PM tickless 停表链
                                                    * 失效(BUG_12, BUG_11 孪生缺陷): 周期模式
                                                    * OsSchedGetNextExpireTime 返回 currTime+rollSum
                                                    * (tick 数被当 cycle 数相加), realSleepTime 语义
                                                    * 错误 -> sleepCycle 恒 0/垃圾值 -> tickLock/
                                                    * timerStart 停表链永不正确执行; 用例断言修复后
                                                    * 契约(timerStart 计数>=1 且编程周期在一个 tick
                                                    * 以上), 修复前置 1 将失败; 隔离点在 It_los_pm.c
                                                    * 聚合注册点(仅周期 tick 板) */
#define LIBC_BUG_13_VSSCANF_LOCK        0 /* ItLostestStdlibSscanfInt001: musl 移植层 vsscanf 在栈上
                                           * 构造伪 FILE{.lock=-1}(sscanf.c/vsscanf.c:14-19), 而移植版
                                           * __lockfile(__lockfile.c:7-10) 无 -1 哨兵检查, 直接
                                           * pthread_mutex_lock((pthread_mutex_t *)-1) -> OsMuxPreCheck
                                           * 解引用 0xffffffff 硬故障(BUG_13, FaultAddr 0xfffffffc,
                                           * qemu 实测 Type=4)。上游 musl __lockfile 对栈 FILE 有
                                           * 免锁路径, 移植改造为 pthread 锁时丢失该分支。
                                           * 修复方向: __lockfile 增加 f->lock<0 直接返回 0。
                                           * 全仓此前无 sscanf 用例(潜伏缺陷); 同族: fscanf 走
                                           * fopen 的真实 FILE(锁有效)不受影响(ItLostestStdioFscanfFile001
                                           * 照常执行, __shgetc 覆盖目标由其承接); 隔离点在
                                           * strtod_test.c 的 RUN_ONE_TESTCASE 注册处, 修复后置 1 恢复 */

/* ==================== fuzz 探针隔离宏(每用例一个) ====================
 * 探针为 sample/fuzz 各模块目录下的正常用例文件 it_<接口描述>_fuzz.c,
 * 用例文件头注释含完整根因分析; 语义同上(0=隔离/1=恢复)。 */
#define FUZZ_BUG_S01_CMSIS_SEMWILD       0 /* osSemaphoreAcquire 野 id: 适配层无 CB 校验直接解引用 */
#define FUZZ_BUG_S04_LOSMEM_DBLFREE      0 /* LOS_MemFree 二次释放: 完整性检查 LOS_PANIC 而非错误码 */
#define FUZZ_BUG_S05_LOSMEM_WILDPOOL     0 /* LOS_MemAlloc 野 pool: 无池头校验, 链表行走访问异常 */
#define FUZZ_BUG_S06_OSEVFLAG_DBLDEL     0 /* osEventFlagsDelete 二次删除: 已销毁链表判空缺失; 同源: LOS_TaskDelete 删除延时态任务挂起(问题13, ItLosTick014 随本宏隔离, 置 1 时一并恢复) */
#define FUZZ_BUG_S07_OSSEM_DBLDEL        0 /* osSemaphoreDelete 二次删除: 已回收 CB 未失效校验 */
#define FUZZ_BUG_S08_OSMUX_ACQ_AFTER_DEL 0 /* osMutexAcquire 于 delete 后: 已回收 CB 未失效校验 */
#define FUZZ_BUG_S09_OSMQ_GET_AFTER_DEL  0 /* osMessageQueueGet 于 delete 后: 已回收 CB 未失效校验 */
#define FUZZ_BUG_S10_OSTHR_DBLTERM       0 /* osThreadTerminate 已终止线程: TCB 回收后无状态校验 */
#define FUZZ_BUG_S11_OSKERNEL_REINIT     0 /* osKernelInitialize Running 态重入(与 BUG_01 同源) */
#define FUZZ_BUG_S13_CLEANUP_NONTASK     0 /* pthread_cleanup_push 于非 pthread 任务: 野指针崩溃(问题39) */
#define FUZZ_BUG_S14_HWI_DEL_SYSTEM      0 /* LOS_HwiDelete 可删系统在用中断: tick 被删看门狗复位(问题40) */

#endif /* _BUG_ISOLATE_H */
