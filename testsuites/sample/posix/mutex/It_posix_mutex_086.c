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
 * 用例名称: ItPosixMutex086
 * 用例描述: 互斥锁资源耗尽与销毁恢复
 *
 * 覆盖目标:
 *   lib/posix/src/pthread_mutex.c:
 *     MapError :68-69        LOS_ERRNO_MUX_ALL_BUSY(耗尽) → EAGAIN
 *     pthread_mutex_init :256  LOS_MuxCreate 失败 → MapError(ret)
 *
 * 断言口径:
 *   循环 pthread_mutex_init 至耗尽(qemu MUX_LIMIT=24 约 20 个, ws63 MUX_LIMIT=60
 *   约 55 个, 精确数量随运行时存量浮动——只断言在配额上限内耗尽):
 *   - 失败那次 init 返回 EAGAIN
 *   - 全部 destroy 后可重建(资源恢复)
 */

/* 用例简要描述: 互斥锁资源耗尽与销毁恢复 */
#include "It_posix_mutex.h"

static UINT32 Testcase(VOID)
{
    pthread_mutex_t muxArray[LOSCFG_BASE_IPC_MUX_LIMIT];
    INT32 created = 0;
    INT32 ret;
    INT32 i;

    /* 循环创建至 mux 耗尽 */
    for (i = 0; i < LOSCFG_BASE_IPC_MUX_LIMIT; i++) {
        ret = pthread_mutex_init(&muxArray[i], NULL);
        if (ret != 0) {
            break;
        }
        created++;
    }
    ICUNIT_GOTO_NOT_EQUAL(created, 0, created, EXIT);
    ICUNIT_GOTO_EQUAL(ret, EAGAIN, ret, EXIT);

    /* 全部销毁, 释放 mux 配额 */
    for (i = 0; i < created; i++) {
        ret = pthread_mutex_destroy(&muxArray[i]);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    created = 0;

    /* 资源恢复后可重建并可用 */
    pthread_mutex_t muxNew;
    ret = pthread_mutex_init(&muxNew, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_mutex_lock(&muxNew);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_NEW);
    ret = pthread_mutex_unlock(&muxNew);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_NEW);
    ret = pthread_mutex_destroy(&muxNew);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_NEW);

EXIT:
    for (i = 0; i < created; i++) {
        (VOID)pthread_mutex_destroy(&muxArray[i]);
    }
    return LOS_OK;
EXIT_NEW:
    (VOID)pthread_mutex_destroy(&muxNew);
    goto EXIT;
}

VOID ItPosixMux086(VOID)
{
    TEST_ADD_CASE("ItPosixMux086", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}
