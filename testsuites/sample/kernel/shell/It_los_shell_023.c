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

static volatile UINT32 g_mdMwTestVar = 0xDEADBEEF;

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    CHAR addrStr[32];
    const CHAR *argvMw[4];
    const CHAR *argvMd[3];

    ret = OsShellInit();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    (VOID)snprintf_s(addrStr, sizeof(addrStr), sizeof(addrStr) - 1, "0x%x", (UINT32)(UINTPTR)&g_mdMwTestVar);

    argvMw[0] = "mw";
    argvMw[1] = addrStr;
    argvMw[2] = "0x12345678";
    argvMw[3] = NULL;
    ret = SystemMw(3, argvMw);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_EQUAL(g_mdMwTestVar, 0x12345678, g_mdMwTestVar);

    g_mdMwTestVar = 0xCAFE0000;
    ret = SystemMw(3, argvMw);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_EQUAL(g_mdMwTestVar, 0x12345678, g_mdMwTestVar);

    argvMd[0] = "md";
    argvMd[1] = addrStr;
    argvMd[2] = NULL;
    ret = SystemMd(2, argvMd);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = SystemMd(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)OS_ERROR, ret);

    ret = SystemMw(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)OS_ERROR, ret);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell023
 * @tc.desc     验证 md/mw 命令: mw 写入内存后读取验证一致性; md 读取内存返回成功;
 *              无参数时返回 OS_ERROR
 */
VOID ItLosShell023(VOID)
{
    TEST_ADD_CASE("ItLosShell023", Testcase, TEST_LOS, TEST_FN_KERNELEXT_SHELL, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL */
