/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2023 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_mem.h"

#ifdef LOSCFG_MEM_MUL_POOL_ALLOC

/* 用例简要描述: 未注册hook时四接口拒绝并打印 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    VOID *ptr = NULL;
    STATIC UINT8 outBuf[16] = {0}; /* 非空非池内指针, 仅参与参数检查 */

    /* 顺序约束(一次性窗口): 本用例必须先于 ItLosMulPool001 执行——
     * g_poolFlag 一旦注册不可逆(los_multipool.c:165-184 无注销 API),
     * 未注册 hook 分支只在首次注册前可触达 */

    /* F-Inv: LOS_MulPoolAlloc 未注册 hook -> 打印 "need register hook" + NULL(219-220) */
    ptr = LOS_MulPoolAlloc(64);
    ICUNIT_GOTO_EQUAL((UINTPTR)ptr, (UINTPTR)NULL, (UINTPTR)ptr, EXIT);

    /* F-Inv: LOS_MulPoolRealloc(ptr!=NULL) 未注册 hook -> 打印 + NULL(261-262) */
    ptr = LOS_MulPoolRealloc((VOID *)outBuf, 64);
    ICUNIT_GOTO_EQUAL((UINTPTR)ptr, (UINTPTR)NULL, (UINTPTR)ptr, EXIT);

    /* F-Inv: LOS_MulPoolShrink 未注册 hook -> 打印 + LOS_NOK(343-344) */
    ret = LOS_MulPoolShrink();
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    /* F-Inv: LOS_MulPoolFree(ptr!=NULL) 未注册 hook -> 打印 + LOS_NOK(374-375) */
    ret = LOS_MulPoolFree((VOID *)outBuf);
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    return LOS_OK;

EXIT:
    return LOS_OK;
}

#endif /* LOSCFG_MEM_MUL_POOL_ALLOC */

VOID ItLosMem332(VOID)
{
#ifdef LOSCFG_MEM_MUL_POOL_ALLOC
    TEST_ADD_CASE("ItLosMem332", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
