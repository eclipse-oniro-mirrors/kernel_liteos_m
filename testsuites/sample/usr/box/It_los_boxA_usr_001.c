/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
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

/* ItLosBoxUsr001 - usr-world dynamic task create through the FULL syscall
 * path.
 *
 * The case body runs in boxA USER mode (the shared iCunit runner invoked
 * from the box test entry): LOS_TaskCreate must bind the COMMON_TEXT usr
 * wrapper (los_usr_api.c: box-heap stack alloc + OsTaskCreateOnlyDynamic
 * veneer -> SVC), NOT the kernel SYS_ implementation (kernel flash, would
 * MemManage-fault on entry - the reverse of the link-route bug that once
 * bound kernel test code to this very wrapper). The child sets a flag and
 * exits through the OsUsrTaskExit trampoline; the recycled user stack is
 * drained back to the box heap by LOS_TaskStackRecycle. */

#include "osTest.h"
#include "syscalls.h"

/* User-stack recycler (kernel/box/los_usr_api.c). No header carries a prototype:
 * the function is a usr-world WRAPPER (it loops OsTaskGetRecycleStack - svc 39 -
 * and frees each stack through the COMMON allocator), not a syscall entry, so
 * the usr include root only #undefs the rename for the name. boxA_main.c and
 * mem/It_los_boxA_usr_mem_util.h declare it the same way. */
extern VOID LOS_TaskStackRecycle(VOID);

/* usr-world compile-time self-check: if the kernel rename root leaked into
 * this TU, LOS_TaskCreate is a macro (#define LOS_TaskCreate SYS_...). In
 * the usr world it must stay a PLAIN symbol - the veneer/wrapper link. */
#ifdef LOS_TaskCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs (testsuites:include / box_syscalls must not reach here)"
#endif

static volatile UINT32 g_usrChildRan = 0;

static VOID UsrChildEntry(VOID)
{
    g_usrChildRan = 1;
    /* return -> OsUsrTaskExit trampoline -> detached delete (kernel) */
}

static UINT32 TestCase(VOID)
{
    UINT32 taskId = 0;
    UINT32 ret;
    UINT32 spin;
    TSK_INIT_PARAM_S param = {0};

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)UsrChildEntry;
    param.uwStackSize = 0x400;      /* box-heap user stack via the wrapper */
    param.pcName = "UsrChildTask";
    param.usTaskPrio = 5;

    g_usrChildRan = 0;
    ret = LOS_TaskCreate(&taskId, &param);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* Child (prio 5) runs on the first yield of this task (prio 0); the
     * bounded poll just tolerates a late first dispatch. Tick is ~1s on
     * this board, so keep the per-iteration delay at 2. */
    for (spin = 0; (g_usrChildRan == 0) && (spin < 20); spin++) {
        LOS_TaskDelay(2);
    }
    ICUNIT_ASSERT_EQUAL(g_usrChildRan, 1, g_usrChildRan);

    /* drain the recycled user stack back to the box heap (usr wrapper) */
    LOS_TaskStackRecycle();
    return LOS_OK;
}

VOID ItLosBoxUsr001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsr001", TestCase, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}
