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
 * 用例名称: ItPosixPthread037
 * 用例描述: 非 pthread 任务调用 pthread 接口被拒(13 处拒绝分支)
 *
 * 覆盖目标:
 *   lib/posix/src/pthread.c 各 API 首行 IsPthread() 拒绝分支(13 处, 一例覆盖 26 行):
 *     :278  pthread_setschedparam   :302 pthread_setschedprio
 *     :320  pthread_once            :351 pthread_setcancelstate
 *     :384  pthread_setcanceltype   :468 pthread_cancel
 *     :538  pthread_detach          :604 pthread_setname_np
 *     :641  pthread_getname_np      :692 pthread_key_create
 *     :730  pthread_key_delete      :771 pthread_setspecific
 *     :812  pthread_getspecific
 *
 * 断言口径:
 *   在 iCunit 主测试任务(非 pthread)上下文逐个调用:
 *   - 12 个返回 int 的接口全部返回 EINVAL
 *   - pthread_getspecific 返回 NULL(拒绝时返回空指针)
 *   目标参数使用主任务自身 id(真实存在但非 pthread)与非法 id 混合, 均先于
 *   参数校验被 IsPthread 拒绝。
 */

/* 用例简要描述: 非 pthread 任务调用 pthread 接口被拒 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1 /* pthread_setname_np/getname_np 声明受特性宏守卫(参照 028/031) */
#endif
#include "It_posix_pthread.h"

static pthread_once_t g_pthread037Once = 0;
static pthread_key_t g_pthread037Key = 0;

static VOID Pthread037OnceRoutine(VOID)
{
    /* 拒绝路径下不应被执行 */
}

static UINT32 Testcase(VOID)
{
    pthread_t self = pthread_self(); /* 主测试任务: 非 pthread */
    struct sched_param sp = {0};
    CHAR nameBuf[16] = {0}; /* 16: task name buffer */
    INT32 ret;
    VOID *specific = NULL;

    sp.sched_priority = LOSCFG_BASE_CORE_TSK_DEFAULT_PRIO;

    /* 1. pthread_setschedparam(:278) */
    ret = pthread_setschedparam(self, SCHED_RR, &sp);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 2. pthread_setschedprio(:302) */
    ret = pthread_setschedprio(self, LOSCFG_BASE_CORE_TSK_DEFAULT_PRIO);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 3. pthread_once(:320, 调用者非 pthread) */
    ret = pthread_once(&g_pthread037Once, Pthread037OnceRoutine);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 4. pthread_setcancelstate(:351, 调用者非 pthread) */
    ret = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 5. pthread_setcanceltype(:384, 调用者非 pthread) */
    ret = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 6. pthread_cancel(:468) */
    ret = pthread_cancel(self);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 7. pthread_detach(:538) */
    ret = pthread_detach(self);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 8. pthread_setname_np(:604) */
    ret = pthread_setname_np(self, "tcases037");
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 9. pthread_getname_np(:641) */
    ret = pthread_getname_np(self, nameBuf, sizeof(nameBuf));
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 10. pthread_key_create(:692, 调用者非 pthread) */
    ret = pthread_key_create(&g_pthread037Key, NULL);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 11. pthread_key_delete(:730, 调用者非 pthread) */
    ret = pthread_key_delete(g_pthread037Key);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 12. pthread_setspecific(:771, 调用者非 pthread) */
    ret = pthread_setspecific(g_pthread037Key, (const VOID *)&ret);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* 13. pthread_getspecific(:812, 调用者非 pthread, 拒绝返回 NULL) */
    specific = pthread_getspecific(g_pthread037Key);
    ICUNIT_ASSERT_EQUAL((UINTPTR)specific, (UINTPTR)NULL, (UINTPTR)specific);

    /* 非法线程 id(超出任务上限)同样被拒 */
    ret = pthread_cancel((pthread_t)(LOSCFG_BASE_CORE_TSK_LIMIT + 1));
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    return LOS_OK;
}

VOID ItPosixPthread037(VOID)
{
    TEST_ADD_CASE("ItPosixPthread037", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
