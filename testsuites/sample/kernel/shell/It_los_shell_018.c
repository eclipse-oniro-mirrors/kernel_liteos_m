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
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER OR
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
#include "shmsg.h"

#if defined(LOSCFG_SHELL) && defined(LOSCFG_SHELL_LK)

/* 测试副作用声明: osCmdReg 无卸载接口, 本用例注册的 "tc_exec18" 命令将永久保留
 * 在命令表中; 命令名使用 "tc_" 前缀, 不与真实命令冲突, 无碍后续用例 */
static volatile UINT32 g_exec18Calls;
static UINT32 g_exec18Argc;
static CHAR g_exec18Argv0[CMD_KEY_LEN];
static CHAR g_exec18Argv1[CMD_KEY_LEN];

/* 安全拷贝: 逐字符拷贝并保证结尾 '\0' */
static VOID ShellExec18KeyCpy(CHAR *dst, const CHAR *src, UINT32 size)
{
    UINT32 i;

    for (i = 0; (i + 1) < size && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* 自定义命令回调: 签名对照 shell.h 中 CmdCallBackFunc 定义 */
static UINT32 ShellExec18Hook(UINT32 argc, const CHAR **argv)
{
    UINT32 i;

    g_exec18Calls++;
    g_exec18Argc = argc;
    for (i = 0; i < CMD_KEY_LEN; i++) {
        g_exec18Argv0[i] = '\0';
        g_exec18Argv1[i] = '\0';
    }
    if (argv != NULL) {
        if (argv[0] != NULL) {
            ShellExec18KeyCpy(g_exec18Argv0, argv[0], sizeof(g_exec18Argv0));
        }
        if (argv[1] != NULL) {
            ShellExec18KeyCpy(g_exec18Argv1, argv[1], sizeof(g_exec18Argv1));
        }
    }
    return LOS_OK;
}

/* 用例简要描述: ExecCmdline 引号路径: 双引号包裹命令名(GetCmdName 跳过引号字符后
 * 正常分发)/双引号包裹含空格参数(OsCmdTokenSplit 引号内不切分为单 token,
 * 引号字符保留在参数中)/单引号字符被 OsCmdKeyShift 过滤后仍可分发 */
static UINT32 Testcase(VOID)
{
    UINT32 ret;

    /* 前置: 确保 shell 命令表已初始化 */
    ret = OsShellInit();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = osCmdReg(CMD_TYPE_EX, "tc_exec18", XARGS, ShellExec18Hook);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 双引号包裹命令名, GetCmdName 遇 '"' 切换引号态并跳过引号字符,
     * 提取出的命令名不含引号, 分发成功且参数原样传递 */
    g_exec18Calls = 0;
    g_exec18Argc = 0xFFFFFFFFU;
    ExecCmdline("\"tc_exec18\" aa");
    ICUNIT_GOTO_EQUAL(g_exec18Calls, 1, g_exec18Calls, EXIT);
    ICUNIT_GOTO_EQUAL(g_exec18Argc, 1, g_exec18Argc, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(g_exec18Argv0, "aa", 0, EXIT);

    /* F-Pos: 双引号包裹含空格参数, 引号内空格不切分为单 token,
     * 引号字符在 OsCmdParseStrdup 拷贝时被剥离,参数为合并后的 aa bb */
    g_exec18Calls = 0;
    g_exec18Argc = 0xFFFFFFFFU;
    ExecCmdline("tc_exec18 \"aa bb\"");
    ICUNIT_GOTO_EQUAL(g_exec18Calls, 1, g_exec18Calls, EXIT);
    ICUNIT_GOTO_EQUAL(g_exec18Argc, 1, g_exec18Argc, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(g_exec18Argv0, "aa bb", 0, EXIT);

    /* F-Pos: 单引号字符被 OsCmdKeyShift 过滤, 命令名提取不受影响 */
    g_exec18Calls = 0;
    g_exec18Argc = 0xFFFFFFFFU;
    ExecCmdline("'tc_exec18' cc dd");
    ICUNIT_GOTO_EQUAL(g_exec18Calls, 1, g_exec18Calls, EXIT);
    ICUNIT_GOTO_EQUAL(g_exec18Argc, 2, g_exec18Argc, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(g_exec18Argv0, "cc", 0, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(g_exec18Argv1, "dd", 0, EXIT);

    return LOS_OK;
EXIT:
    return LOS_OK;
}

/*
 * @tc.name     ItLosShell018
 * @tc.desc     验证 ExecCmdline 对外接口的引号处理路径: 双引号包裹命令名
 *              (GetCmdName 跳过引号字符)、双引号包裹含空格参数(引号内不切分、
 *              引号字符保留)、单引号被 OsCmdKeyShift 过滤后正常分发;
 *              断言回调计数/argc/argv 内容
 */
VOID ItLosShell018(VOID)
{
    TEST_ADD_CASE("ItLosShell018", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL && LOSCFG_SHELL_LK */
