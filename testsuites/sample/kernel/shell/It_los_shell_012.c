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
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#if defined(LOSCFG_SHELL) && defined(LOSCFG_FS_LITTLEFS)

#define TC12_DIR   "/littlefs/tc12"
#define TC12_FILE  "/littlefs/tc12/f1.txt"

/* 用例简要描述: ls/cd/pwd 命令成功路径直调: 子目录枚举(含 stat 目录/文件两个判定
 * 分支)、无参 ls 枚举工作目录、cd 切换后 OsShellGetWorkingDirtectory 校验、
 * cd 到文件(opendir ENOTDIR 失败)、cd ".."、cd "/."、cd "/.."、pwd 打印。
 * 用例自包含: 仅创建并清理自身夹具, 无挂载/句柄等全局副作用 */
static UINT32 Testcase(VOID)
{
    INT32 ret;
    INT32 fd = -1;
    const CHAR *argvDir[1] = {TC12_DIR};
    const CHAR *argvDirSlash[1] = {TC12_DIR "/"};
    const CHAR *argvFile[1] = {TC12_FILE};
    const CHAR *argvUp[1] = {".."};
    const CHAR *argvDot[1] = {TC12_DIR "/."};
    const CHAR *argvDotDot[1] = {TC12_DIR "/.."};

    /* 前置: 创建被枚举/切换的目录与文件(POSIX 对外接口搭台) */
    ret = mkdir(TC12_DIR, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC12_FILE, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "HELLO", 5); /* 5, sizeof "HELLO" */
    ICUNIT_GOTO_EQUAL(ret, 5, ret, EXIT);
    (VOID)close(fd);
    fd = -1;

    /* F-Pos: ls 子目录(path[1]!='\0' 分支,含目录与文件两种 stat 判定) */
    ret = OsShellCmdLs(1, argvDir);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Pos: ls 带尾斜杠路径 */
    ret = OsShellCmdLs(1, argvDirSlash);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Pos: ls 无参(枚举工作目录 "/") */
    ret = OsShellCmdLs(0, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Pos: cd 进入子目录 */
    ret = OsShellCmdCd(1, argvDir);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(OsShellGetWorkingDirtectory(), TC12_DIR, 0, EXIT);

    /* F-Pos: pwd 无参(打印当前工作目录) */
    ret = OsShellCmdPwd(0, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Pos: ls 无参(枚举非根工作目录) */
    ret = OsShellCmdLs(0, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Inv: cd 到普通文件(opendir ENOTDIR 失败,cwd 不变) */
    ret = OsShellCmdCd(1, argvFile);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(OsShellGetWorkingDirtectory(), TC12_DIR, 0, EXIT);

    /* F-Pos: cd ".."(相对路径回溯) */
    ret = OsShellCmdCd(1, argvUp);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(OsShellGetWorkingDirtectory(), "/littlefs", 0, EXIT);

    /* F-Pos: cd 尾部 "/."(等价进入) */
    ret = OsShellCmdCd(1, argvDot);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(OsShellGetWorkingDirtectory(), TC12_DIR, 0, EXIT);

    /* F-Pos: cd 尾部 "/.."(等价回溯) */
    ret = OsShellCmdCd(1, argvDotDot);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_STRING_EQUAL(OsShellGetWorkingDirtectory(), "/littlefs", 0, EXIT);

EXIT:
    /* 异常路径同样清理: 关闭残留 fd, 恢复工作目录, 删除夹具(尽力而为) */
    if (fd >= 0) {
        (VOID)close(fd);
    }
    (VOID)OsShellCmdCd(0, NULL);
    (VOID)unlink(TC12_FILE);
    (VOID)rmdir(TC12_DIR);
    return LOS_OK;
}

/*
 * @tc.name     ItLosShell012
 * @tc.desc     验证 ls/cd/pwd 成功路径: 子目录枚举与 stat 目录文件判定分支,
 *              无参 ls 枚举工作目录, cd 切换/回溯("/." "/.." "..")后
 *              OsShellGetWorkingDirtectory 结果校验, cd 普通文件失败保持 cwd,
 *              pwd 无参打印。依赖 qemu /littlefs 挂载, 用例自包含无全局副作用
 */
VOID ItLosShell012(VOID)
{
    TEST_ADD_CASE("ItLosShell012", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL && LOSCFG_FS_LITTLEFS */
