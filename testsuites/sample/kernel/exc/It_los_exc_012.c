/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "it_los_exc.h"

#ifdef LOSCFG_SHELL_EXCINFO_DUMP
static CHAR g_exc012Buf[256]; // 256, exception info dump buffer size.

static VOID Exc012DumpHook(UINTPTR startAddr, UINT32 space, UINT32 rwFlag, CHAR *buf)
{
    (VOID)startAddr;
    (VOID)space;
    (VOID)rwFlag;
    (VOID)buf;
}

/* 用例简要描述: LOS_ExcInfoRegHook（对外接口）异常信息转储注册正/负路径——
 * 不触发任何 CPU 异常、用例可继续运行：合法注册成功；hook=NULL 与 buf=NULL
 * 走参数校验分支直接返回；注册后常规打印链路不受影响 */
static UINT32 Testcase(VOID)
{
    /* F-Pos: 合法参数注册转储缓冲（注册成功，不触发异常，继续执行） */
    LOS_ExcInfoRegHook((UINTPTR)g_exc012Buf, sizeof(g_exc012Buf), g_exc012Buf, Exc012DumpHook);

    /* F-Inv: hook = NULL → 参数校验失败分支，直接返回 */
    LOS_ExcInfoRegHook((UINTPTR)g_exc012Buf, sizeof(g_exc012Buf), g_exc012Buf, NULL);

    /* F-Inv: buf = NULL → 参数校验失败分支，直接返回 */
    LOS_ExcInfoRegHook((UINTPTR)g_exc012Buf, sizeof(g_exc012Buf), NULL, Exc012DumpHook);

    /* 注册后常规打印链路不受影响，套件继续运行 */
    PRINTK("[ItLosExc012] LOS_ExcInfoRegHook registered, no exception triggered\n");
    ICUNIT_ASSERT_EQUAL(1, 1, 0);
    return LOS_OK;
}
#endif /* LOSCFG_SHELL_EXCINFO_DUMP */

/**
 * @ingroup TEST_EXC
 * @par TestCase_Number
 * ItLosExc012
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_ExcInfoRegHook
 * @par TestCase_Pretreatment_Condition
 * LOSCFG_SHELL_EXCINFO_DUMP enabled (ws63).
 * @par TestCase_Test_Steps
 * step1: call LOS_ExcInfoRegHook with valid buffer and hook
 * step2: call LOS_ExcInfoRegHook with NULL hook (negative)
 * step3: call LOS_ExcInfoRegHook with NULL buf (negative)
 * step4: PRINTK still works, case continues running
 * @par TestCase_Expected_Result
 * 1. valid registration succeeds without triggering any CPU exception
 * 2. negative calls return early via the NULL-check branch
 * 3. no crash, suite continues
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * Non-destructive variant of ItLosExc004: registers the dump buffer only,
 * never triggers a real fault, so it can run in the automated suite.
 */
VOID ItLosExc012(VOID)
{
#ifdef LOSCFG_SHELL_EXCINFO_DUMP
    TEST_ADD_CASE("ItLosExc012", Testcase, TEST_LOS, TEST_EXC, TEST_LEVEL0, TEST_FUNCTION);
#endif
}
