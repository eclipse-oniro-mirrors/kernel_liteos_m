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
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 用例名称: ItPosixRwlock081
 * 用例描述: rwlock 慢路径 RwList 链头越界写邻槽 stackPointer(BUG_17)
 *
 * 【现象】
 *   qemu fuzz 全灭: ItFuzzPosixRwlock 执行中 TracePacker 任务 SP error 挂死
 *   (调度器 OsTaskSwitchCheck SP 检查失败合成 EXC_STACKOVERFLOW)。
 *   gdb 硬件监视点指令级闭环: 持锁窗口内邻槽 stackPointer 被写成 malloc
 *   节点堆指针。任意"加锁任务下一槽为活任务"布局均可命中(与 HPL 无关,
 *   98c72546 引入时越界落点在本 TCB 尾部 padding 无害蛰伏, !1212(93769320)
 *   新增 hpl 字段吸收 padding 后落点变为 TCB[i+1].stackPointer 始生效)。
 * 【根因】
 *   lib/posix/src/pthread_rwlock.c OsRwlockAddNode/OsRwlockRmvNode 把
 *   &runTsk->rwList(LosTaskCB 4 字节末字段)当 RwList({ptr,next})链头,
 *   head->next(+4)越出 TCB 边界, 读写别名下一任务槽 stackPointer。
 * 【修复方向】
 *   struct RwList 字段序调整为 {next, ptr}: head->next 恰落于 TCB rwList
 *   字段内, 所有链头访问不再越界(节点访问全用具名成员, 布局/语义不变)。
 * 【恢复方法】
 *   本用例恢复注册执行, 作为回归看护用例常跑。
 *
 * 断言口径(修复后契约):
 *   测试任务在关中断窗口内快照邻槽(自身 TCB 起偏移 sizeof(LosTaskCB) 处,
 *   即缺陷越界写落点)stackPointer, 无争用 rdlock/wrlock(同一
 *   OsRwlockAddNode 调用点, 加锁窗口无调度)后持锁探测:
 *   - 加锁返回 0;
 *   - 持锁期间邻槽 stackPointer 与快照严格相等(未修复=malloc 堆指针, 红灯)。
 */

/* 用例简要描述: rwlock 链头越界写邻槽 stackPointer(BUG_17) */
#include "It_posix_rwlock.h"

static pthread_rwlock_t g_rwl081;

static UINT32 Testcase(VOID)
{
    LosTaskCB *neighbor = NULL;
    UINT32 tid;
    UINTPTR intSave;
    UINTPTR spBefore;
    UINTPTR spHold;
    INT32 ret;

    ret = pthread_rwlock_init(&g_rwl081, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    tid = LOS_CurTaskIDGet();
    /* 邻槽必须仍在任务数组内(末槽无邻槽) */
    ICUNIT_ASSERT_EQUAL(tid < LOSCFG_BASE_CORE_TSK_LIMIT, TRUE, tid);

    neighbor = (LosTaskCB *)((CHAR *)OS_TCB_FROM_TID(tid) + sizeof(LosTaskCB));

    intSave = LOS_IntLock();
    spBefore = (UINTPTR)neighbor->stackPointer;
    ret = pthread_rwlock_rdlock(&g_rwl081);
    spHold = (UINTPTR)neighbor->stackPointer;
    LOS_IntRestore(intSave);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_EQUAL(spHold, spBefore, spHold);

    ret = pthread_rwlock_unlock(&g_rwl081);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    intSave = LOS_IntLock();
    spBefore = (UINTPTR)neighbor->stackPointer;
    ret = pthread_rwlock_wrlock(&g_rwl081);
    spHold = (UINTPTR)neighbor->stackPointer;
    LOS_IntRestore(intSave);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_EQUAL(spHold, spBefore, spHold);

    ret = pthread_rwlock_unlock(&g_rwl081);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_rwlock_destroy(&g_rwl081);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return LOS_OK;
}

VOID ItPosixRwlock081(VOID)
{
    TEST_ADD_CASE("ItPosixRwlock081", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}
