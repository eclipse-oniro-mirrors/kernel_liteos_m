/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* ItLosBoxUsr017 - scenario 3 (stack-reuse OFF form): static create with an
 * illegal stack top must be rejected and leave the pool untouched.
 *
 * Runs in boxA USER mode: the top stack must be aligned and inside the box
 * data window. */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root"
#endif

/* >= LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE (1024) so the size check passes
 * before the alignment / window checks are exercised */
static UINT64 g_usr017Stack[256] __attribute__((aligned(8)));

static VOID Usr017Child(VOID)
{
}

static UINT32 TestCase(VOID)
{
    UINT32 taskId = 0;
    UINT32 ret;
    TSK_INIT_PARAM_S param = {0};

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)Usr017Child;
    param.uwStackSize = sizeof(g_usr017Stack);
    param.pcName = "Usr017Child";
    param.usTaskPrio = 5;

    /* 1) stack top not aligned */
    ret = LOS_TaskCreateStatic(&taskId, &param, (VOID *)((UINTPTR)g_usr017Stack + 4));
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_STKSZ_NOT_ALIGN, ret);

    /* 2) stack top outside the box data window */
    ret = LOS_TaskCreateStatic(&taskId, &param, (VOID *)0x10000000);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_ID_INVALID, ret);

    /* the legal stack still works: the rejected creates left no residue */
    ret = LOS_TaskCreateOnlyStatic(&taskId, &param, (VOID *)g_usr017Stack);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_TaskDelete(taskId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    LOS_TaskStackRecycle();
    return LOS_OK;
}

VOID ItLosBoxUsr017(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsr017", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
