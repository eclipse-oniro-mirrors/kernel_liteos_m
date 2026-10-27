/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_shell.h"
#include "shcmd.h"
#include "securec.h"

#ifdef LOSCFG_SHELL

static UINT32 Testcase(VOID)
{
    INT32 ret;
    CHAR cmdBuf[SHOW_MAX_LEN];
    UINT32 len;

    ret = OsShellInit();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    (VOID)memset_s(cmdBuf, SHOW_MAX_LEN, 0, SHOW_MAX_LEN);
    (VOID)strncpy_s(cmdBuf, SHOW_MAX_LEN, "tas", 3);
    len = 3;
    ret = OsTabCompletion(cmdBuf, &len);
    ICUNIT_ASSERT_EQUAL(ret > 0, 1, ret);
    ICUNIT_ASSERT_EQUAL(strncmp(cmdBuf, "task", 4), 0, 0);
    ICUNIT_ASSERT_EQUAL(len, 4, len);

    (VOID)memset_s(cmdBuf, SHOW_MAX_LEN, 0, SHOW_MAX_LEN);
    (VOID)strncpy_s(cmdBuf, SHOW_MAX_LEN, "fre", 3);
    len = 3;
    ret = OsTabCompletion(cmdBuf, &len);
    ICUNIT_ASSERT_EQUAL(ret > 0, 1, ret);
    ICUNIT_ASSERT_EQUAL(strncmp(cmdBuf, "free", 4), 0, 0);
    ICUNIT_ASSERT_EQUAL(len, 4, len);

    (VOID)memset_s(cmdBuf, SHOW_MAX_LEN, 0, SHOW_MAX_LEN);
    (VOID)strncpy_s(cmdBuf, SHOW_MAX_LEN, "hel", 3);
    len = 3;
    ret = OsTabCompletion(cmdBuf, &len);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);
    ICUNIT_ASSERT_EQUAL(strncmp(cmdBuf, "help", 4), 0, 0);
    ICUNIT_ASSERT_EQUAL(len, 4, len);

    (VOID)memset_s(cmdBuf, SHOW_MAX_LEN, 0, SHOW_MAX_LEN);
    (VOID)strncpy_s(cmdBuf, SHOW_MAX_LEN, "zzz", 3);
    len = 3;
    ret = OsTabCompletion(cmdBuf, &len);
    ICUNIT_ASSERT_EQUAL(ret != 1, 1, ret);

    ret = OsTabCompletion(NULL, &len);
    ICUNIT_ASSERT_EQUAL(ret, (INT32)OS_ERROR, ret);

    (VOID)memset_s(cmdBuf, SHOW_MAX_LEN, 0, SHOW_MAX_LEN);
    ret = OsTabCompletion(cmdBuf, NULL);
    ICUNIT_ASSERT_EQUAL(ret, (INT32)OS_ERROR, ret);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell028
 * @tc.desc     验证 Tab 命令补全: "tas"->"task"(返回>0,len=4);
 *              "fre"->"free"(返回>0,len=4); "hel"->"help"(返回1,len=4);
 *              "zzz"无匹配(返回0); NULL参数防御(返回OS_ERROR)
 */
VOID ItLosShell028(VOID)
{
    TEST_ADD_CASE("ItLosShell028", Testcase, TEST_LOS, TEST_FN_KERNELEXT_SHELL, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL */
