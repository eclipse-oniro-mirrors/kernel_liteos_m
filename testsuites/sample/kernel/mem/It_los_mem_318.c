/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
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

#ifdef LOSCFG_KERNEL_MEM_TLSF
#if (LOSCFG_MEM_LEAKCHECK == 1)
/* 用例简要描述: NULL池入参早退打印不崩溃 */
static UINT32 TestCase(VOID)
{
    /* F-Inv: LOS_MemUsedNodeShow 入口无 NULL 预检, NULL 池直达
     * OsAllMemNodeDoHandle 的 pool==NULL 打印早退分支(tlsf los_memory.c:238-239),
     * 仅打印 "input param is NULL" 后安全返回, 无崩溃无资源操作 */
    LOS_MemUsedNodeShow(NULL);

    return LOS_OK;
}
#endif /* LOSCFG_MEM_LEAKCHECK == 1 */
#endif /* LOSCFG_KERNEL_MEM_TLSF */

VOID ItLosMem318(VOID)
{
#ifdef LOSCFG_KERNEL_MEM_TLSF
#if (LOSCFG_MEM_LEAKCHECK == 1)
    TEST_ADD_CASE("ItLosMem318", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL0, TEST_FUNCTION);
#endif
#endif
}
