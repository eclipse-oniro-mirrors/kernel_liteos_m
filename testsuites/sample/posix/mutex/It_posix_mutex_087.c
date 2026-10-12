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
 * 用例名称: ItPosixMutex087
 * 用例描述: 非法协议属性锁的操作被拒
 *
 * 覆盖目标:
 *   lib/posix/src/pthread_mutex.c:
 *     CheckMutexAttr :294  attr.protocol > PTHREAD_PRIO_PROTECT → LOS_NOK
 *     OsMuxPreCheck  :316  CheckMutexAttr 失败 → EINVAL(lock/trylock 拒绝)
 *
 * 断言口径:
 *   直接构造 attr.protocol=3(绕过 pthread_mutexattr_setprotocol 的范围校验,
 *   结构体为 musl porting 公开 ABI: {unsigned char protocol; prioceiling; type; reserved}):
 *   - pthread_mutex_init(带该 attr) 返回 0(init 不校验, 仅拷贝属性)
 *   - pthread_mutex_lock == EINVAL(首次加锁走 PreCheck attr 拒绝)
 *   - pthread_mutex_trylock == EINVAL
 *   - destroy 返回 0(init 已创建内核 mux, 必须回收)
 */

/* 用例简要描述: 非法协议属性锁的操作被拒 */
#include "It_posix_mutex.h"

static UINT32 Testcase(VOID)
{
    pthread_mutex_t mux;
    pthread_mutexattr_t attr;
    INT32 ret;

    ret = pthread_mutexattr_init(&attr);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 直接构造非法协议值 3(> PTHREAD_PRIO_PROTECT=2, 绕过 setprotocol 校验) */
    attr.protocol = 3;

    ret = pthread_mutex_init(&mux, &attr);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_ATTR);

    /* PreCheck → CheckMutexAttr :294 拒绝 → EINVAL */
    ret = pthread_mutex_lock(&mux);
    ICUNIT_GOTO_EQUAL(ret, EINVAL, ret, EXIT);

    ret = pthread_mutex_trylock(&mux);
    ICUNIT_GOTO_EQUAL(ret, EINVAL, ret, EXIT);

    /* mux 已占用内核配额, 必须 destroy 回收 */
    ret = pthread_mutex_destroy(&mux);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_ATTR);

    ret = pthread_mutexattr_destroy(&attr);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return LOS_OK;
EXIT:
    (VOID)pthread_mutex_destroy(&mux);
EXIT_ATTR:
    (VOID)pthread_mutexattr_destroy(&attr);
    return LOS_NOK;
}

VOID ItPosixMux087(VOID)
{
    TEST_ADD_CASE("ItPosixMux087", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}
