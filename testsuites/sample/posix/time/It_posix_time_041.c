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
 * 用例名称: ItPosixTime041
 * 用例描述: 软件定时器资源耗尽与回收
 *
 * 覆盖目标:
 *   lib/posix/src/time.c:
 *     timer_create :166-167  LOS_SwtmrCreate 返回 LOS_ERRNO_SWTMR_MAXSIZE →
 *                             errno=EAGAIN(liteos-m 单次触发 swtmr 池上限:
 *                             qemu SWTMR_LIMIT=48 / ws63 SWTMR_LIMIT=16)
 *
 * 断言口径:
 *   循环 timer_create 至耗尽(在 LOSCFG_BASE_CORE_SWTMR_LIMIT 内):
 *   - 失败那次返回 -1 且 errno==EAGAIN
 *   - 逐个 timer_delete 全部回收后可再建(资源恢复)
 */

/* 用例简要描述: 软件定时器资源耗尽与回收 */
#include "It_posix_time.h"

static VOID Time041Callback(union sigval v)
{
    (VOID)v;
}

static UINT32 Testcase(VOID)
{
    timer_t timers[LOSCFG_BASE_CORE_SWTMR_LIMIT];
    struct sigevent evp = {0};
    INT32 created = 0;
    INT32 ret;
    INT32 i;

    for (i = 0; i < LOSCFG_BASE_CORE_SWTMR_LIMIT; i++) {
        timers[i] = (timer_t)0;
    }
    evp.sigev_notify = SIGEV_THREAD;
    evp.sigev_notify_function = Time041Callback;

    /* 循环创建至 swtmr 池耗尽 */
    for (i = 0; i < LOSCFG_BASE_CORE_SWTMR_LIMIT; i++) {
        ret = timer_create(CLOCK_REALTIME, &evp, &timers[i]);
        if (ret != 0) {
            break;
        }
        created++;
    }
    ICUNIT_GOTO_NOT_EQUAL(created, 0, created, EXIT);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EAGAIN, errno, EXIT);

    /* 逐个删除回收 */
    for (i = 0; i < created; i++) {
        ret = timer_delete(timers[i]);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        timers[i] = (timer_t)0;
    }
    created = 0;

    /* 资源恢复后可再建 */
    timer_t timerNew = (timer_t)0;
    ret = timer_create(CLOCK_REALTIME, &evp, &timerNew);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = timer_delete(timerNew);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    for (i = 0; i < created; i++) {
        if (timers[i] != (timer_t)0) {
            (VOID)timer_delete(timers[i]);
        }
    }
    return LOS_NOK;
}

VOID ItPosixTime041(VOID)
{
    TEST_ADD_CASE("ItPosixTime041", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}
