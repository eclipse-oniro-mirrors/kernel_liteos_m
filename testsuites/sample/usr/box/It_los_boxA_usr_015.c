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

/* ItLosBoxUsr015 - usr-world LOS_TaskEventRead (current task) /
 * LOS_TaskEventWrite (target task) through the syscall path.
 *
 * The child parks on its own task event; the runner writes the child's event
 * by task id, waking it. */

#include "osTest.h"
#include "syscalls.h"

#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root"
#endif

#define USR_EVENT_MASK 0x1

static volatile UINT32 g_usr015Woke = 0;

static VOID Usr015Child(VOID)
{
    UINT32 ret = LOS_TaskEventRead(USR_EVENT_MASK, 20); /* bounded wait */
    if (ret == USR_EVENT_MASK) {
        g_usr015Woke = 1;
    }
}

static UINT32 TestCase(VOID)
{
    UINT32 taskId = 0;
    UINT32 ret;
    UINT32 spin;
    TSK_INIT_PARAM_S param = {0};

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)Usr015Child;
    param.uwStackSize = 0x400;
    param.pcName = "Usr015Child";
    param.usTaskPrio = 5;

    g_usr015Woke = 0;
    ret = LOS_TaskCreate(&taskId, &param);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* give the child a window to park on its task event */
    LOS_TaskDelay(2);

    ret = LOS_TaskEventWrite(taskId, USR_EVENT_MASK);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    for (spin = 0; (g_usr015Woke == 0) && (spin < 20); spin++) {
        LOS_TaskDelay(2);
    }
    ICUNIT_ASSERT_EQUAL(g_usr015Woke, 1, g_usr015Woke);

    LOS_TaskStackRecycle();
    return LOS_OK;
}

VOID ItLosBoxUsr015(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsr015", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
