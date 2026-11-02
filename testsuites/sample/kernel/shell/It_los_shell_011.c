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

#if defined(LOSCFG_SHELL) && defined(LOSCFG_FS_VFS)

/* 用例简要描述: vfs 系列 shell 命令(ls/cat/mkdir/pwd/touch/cp/rm/rmdir/cd)参数错误与
 * 规范化失败路径直调,覆盖 show.h 的 SetErr/SetErrno 内联函数;
 * 所选输入在有无文件系统的板上返回值均确定一致(不依赖 FS 成功路径) */
static UINT32 Testcase(VOID)
{
    INT32 ret;
    const CHAR *argvLs2[2] = {"/littlefs", "extra"};
    const CHAR *argvDbl[1] = {"//x"};
    const CHAR *argvMiss[1] = {"/littlefs/nonexist_dir_011"};
    const CHAR *argvMissSub[1] = {"/littlefs/nonexist_dir_011/sub"};
    const CHAR *argvMissFile[1] = {"/littlefs/nonexist_dir_011/f"};
    const CHAR *argv2[2] = {"p", "q"};
    const CHAR *argvRm3[3] = {"-r", "-R", "x"};
    const CHAR *argvRmBadOpt[2] = {"-x", "/d"};
    const CHAR *argvRmR[2] = {"-r", "//x"};
    const CHAR *argvCpSrcDbl[2] = {"//x", "/d"};
    const CHAR *argvCpSrcSlash[2] = {"abc/", "/d"};
    const CHAR *argvCpDstDbl[2] = {"/littlefs/a", "//x"};
    const CHAR *argvNull[1] = {NULL};
    static CHAR longPath[300];
    const CHAR *argvLong[1] = {NULL};

    /* F-Inv: ls 多参数(Usage 提示) */
    ret = OsShellCmdLs(2, argvLs2);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: ls 规范化失败("//" 开头,SetErr 路径) */
    ret = OsShellCmdLs(1, argvDbl);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Pos: ls 不存在目录(opendir 失败打印,返回 0) */
    ret = OsShellCmdLs(1, argvMiss);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Inv: cat 参数个数为 0/2 */
    ret = OsShellCmdCat(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = OsShellCmdCat(2, argv2);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: cat 规范化失败 */
    ret = OsShellCmdCat(1, argvDbl);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: cat 打开失败(文件不存在) */
    ret = OsShellCmdCat(1, argvMissFile);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: mkdir 参数个数为 0/2(打印 Usage,均返回 0) */
    ret = OsShellCmdMkdir(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = OsShellCmdMkdir(2, argv2);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Inv: mkdir 规范化失败 */
    ret = OsShellCmdMkdir(1, argvDbl);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Pos: mkdir 父目录不存在(mkdir 失败 perror,仍返回 0) */
    ret = OsShellCmdMkdir(1, argvMissSub);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Inv: pwd 带参数 */
    ret = OsShellCmdPwd(1, argv2);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Pos: pwd 无参数 */
    ret = OsShellCmdPwd(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Inv: touch 参数个数为 0/2 */
    ret = OsShellCmdTouch(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = OsShellCmdTouch(2, argv2);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: touch 规范化失败 */
    ret = OsShellCmdTouch(1, argvDbl);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: touch 打开失败(父目录不存在) */
    ret = OsShellCmdTouch(1, argvMissFile);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: cp 参数不足 */
    ret = OsShellCmdCp(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ret = OsShellCmdCp(1, argvMiss);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: cp 源规范化失败 */
    ret = OsShellCmdCp(2, argvCpSrcDbl);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: cp 源以 '/' 结尾(拒绝目录源) */
    ret = OsShellCmdCp(2, argvCpSrcSlash);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: cp 目标规范化失败 */
    ret = OsShellCmdCp(2, argvCpDstDbl);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: rm 参数个数为 3(PrintRmUsage) */
    ret = OsShellCmdRm(3, argvRm3);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: rm 非法选项(PrintRmUsage) */
    ret = OsShellCmdRm(2, argvRmBadOpt);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: rm -r 规范化失败 */
    ret = OsShellCmdRm(2, argvRmR);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: rm 单参规范化失败 */
    ret = OsShellCmdRm(1, argvDbl);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Pos: rm 不存在文件(stat 失败+perror,返回 0) */
    ret = OsShellCmdRm(1, argvMissFile);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Inv: rmdir 无参数 */
    ret = OsShellCmdRmdir(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Inv: rmdir 规范化失败 */
    ret = OsShellCmdRmdir(1, argvDbl);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);

    /* F-Pos: rmdir 不存在目录(打印失败,返回 0) */
    ret = OsShellCmdRmdir(1, argvMiss);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Pos: cd 无参数(切换到根) */
    ret = OsShellCmdCd(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_STRING_EQUAL(OsShellGetWorkingDirtectory(), "/", 0);

    /* F-Pos: cd NULL 参数(OsShellCmdDoChdir path==NULL 分支: 打印工作目录) */
    ret = OsShellCmdCd(1, argvNull);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_STRING_EQUAL(OsShellGetWorkingDirtectory(), "/", 0);

    /* F-Inv: cd 超长路径(strlen > PATH_MAX,SetErr ENOTDIR) */
    (VOID)memset_s(longPath, sizeof(longPath), 'p', sizeof(longPath) - 1);
    longPath[sizeof(longPath) - 1] = '\0';
    argvLong[0] = longPath;
    ret = OsShellCmdCd(1, argvLong);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_STRING_EQUAL(OsShellGetWorkingDirtectory(), "/", 0);

    /* F-Inv: cd 规范化失败("//" 开头) */
    ret = OsShellCmdCd(1, argvDbl);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_STRING_EQUAL(OsShellGetWorkingDirtectory(), "/", 0);

    /* F-Inv: cd 不存在目录(opendir 失败) */
    ret = OsShellCmdCd(1, argvMiss);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_STRING_EQUAL(OsShellGetWorkingDirtectory(), "/", 0);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell011
 * @tc.desc     验证 vfs shell 命令参数错误与路径规范化失败分支: ls/cat/mkdir/touch/
 *              cat/cp/rm/rmdir 的 argc 非法与 "//" 开头规范化失败(SetErr 内联路径),
 *              不存在路径的 opendir/open/stat 失败打印路径,cd 的 NULL 参数/超长路径/
 *              不存在目录下工作目录保持不变。所选输入不依赖文件系统成功路径,
 *              双板(qemu/ws63)返回值一致
 */
VOID ItLosShell011(VOID)
{
    TEST_ADD_CASE("ItLosShell011", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL && LOSCFG_FS_VFS */
