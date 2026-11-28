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
#include "sys/time.h"
#include "time.h"
#include "string.h"

#ifdef LOSCFG_SHELL

#define DATE_ERR (-1)

static UINT32 Testcase(VOID)
{
    INT32 ret;
    struct timeval tvBefore;
    struct timeval tvAfter;

    ret = OsShellInit();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = gettimeofday(&tvBefore, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    const CHAR *argvDateOnly[1] = {NULL};
    ret = OsShellCmdDate(1, argvDateOnly);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    const CHAR *argvHelp[2] = {"date", "--help"};
    ret = OsShellCmdDate(2, argvHelp);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    const CHAR *argvFmtY[2] = {"date", "+%Y"};
    ret = OsShellCmdDate(2, argvFmtY);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    const CHAR *argvFmtHMS[2] = {"date", "+%H:%M:%S"};
    ret = OsShellCmdDate(2, argvFmtHMS);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    const CHAR *argvSetBad[3] = {"date", "-s", "notadate"};
    ret = OsShellCmdDate(3, argvSetBad);
    ICUNIT_ASSERT_EQUAL(ret, (INT32)DATE_ERR, ret);

    ret = gettimeofday(&tvAfter, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 正向路径: 8 字符 yyyymmdd 为本实现支持的合法 -s 格式,必须真实设置成功 */
    const CHAR *argvSetDate[3] = {"date", "-s", "20260101"};
    ret = OsShellCmdDate(3, argvSetDate);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 防御: 非法月份必须被拒绝(本断言只锁定拒绝行为;越界读修复属 P1-2 另行裁决) */
    const CHAR *argvSetBadMon[3] = {"date", "-s", "2026/13/40"};
    ret = OsShellCmdDate(3, argvSetBadMon);
    ICUNIT_ASSERT_EQUAL(ret, (INT32)DATE_ERR, ret);

    ret = gettimeofday(&tvAfter, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell025
 * @tc.desc     验证 date 命令增强: 无参显示时间(返回0); --help(返回0);
 *              +%Y/%H:%M:%S 格式化输出(返回0); -s 设置无效时间(返回DATE_ERR);
 *              -s 设置有效时间 20260101(返回0); -s 非法月份 13/日 40(返回DATE_ERR)
 */
VOID ItLosShell025(VOID)
{
    TEST_ADD_CASE("ItLosShell025", Testcase, TEST_LOS, TEST_FN_KERNELEXT_SHELL, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL */
