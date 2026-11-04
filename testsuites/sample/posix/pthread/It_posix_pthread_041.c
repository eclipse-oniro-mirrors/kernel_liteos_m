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
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 用例名称: ItPosixPthread041
 * 用例描述: 调度锁内 sched_yield 拒绝与解锁恢复
 *
 * 覆盖目标:
 *   lib/posix/src/sched.c:
 *     sched_yield :50  LOS_TaskYield 在调度锁内返回非 OK(非 YIELD_NOT_ENOUGH_TASK)
 *                     → return -1
 *
 * 断言口径:
 *   LOS_TaskLock 后 sched_yield() == -1(拒绝)
 *   LOS_TaskUnlock 后 sched_yield() == 0(恢复; 空闲板可能返回
 *   YIELD_NOT_ENOUGH_TASK, 实现将其视为成功返回 0)
 */

/* 用例简要描述: 调度锁内 sched_yield 拒绝恢复 */
#include "It_posix_pthread.h"
#include <sched.h>

static UINT32 Testcase(VOID)
{
    INT32 ret;

    /* 调度锁内: LOS_TaskYield 失败 → -1 */
    LOS_TaskLock();
    ret = sched_yield();
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_UNLOCK);
    LOS_TaskUnlock();

    /* 解锁后恢复: OK 或 YIELD_NOT_ENOUGH_TASK 均返回 0 */
    ret = sched_yield();
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    return LOS_OK;
EXIT_UNLOCK:
    LOS_TaskUnlock();
    return LOS_NOK;
}

VOID ItPosixPthread041(VOID)
{
    TEST_ADD_CASE("ItPosixPthread041", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
