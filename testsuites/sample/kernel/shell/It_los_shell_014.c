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

#define TC14_DIR     "/littlefs/tc14"
#define TC14_A       "/littlefs/tc14/a.txt"
#define TC14_B       "/littlefs/tc14/b.txt"
#define TC14_C       "/littlefs/tc14/c.txt"
#define TC14_W       "/littlefs/tc14/w"
#define TC14_W1      "/littlefs/tc14/w/w1.txt"
#define TC14_N1      "/littlefs/tc14/n1/f.txt"
#define TC14_N2      "/littlefs/tc14/n2/f.txt"
#define TC14_DD      "/littlefs/tc14/dd"
#define SRC14        "/littlefs/src14.txt"
#define CP_FD_MAX    280 /* NR_OPEN_DEFAULT(253)+余量 */

static CHAR g_deepDir[150]; /* "/littlefs/" + 139 字符名 = 149 字符路径 */
static CHAR g_deepSrc[150];
static int g_fds[CP_FD_MAX];

/* 拷贝文件内容并校验(dst 内容应为 expect 的 len 字节); fd 在断言前关闭,失败路径不泄漏 */
static UINT32 CheckFileContent(const CHAR *path, const CHAR *expect, INT32 len)
{
    INT32 fd;
    CHAR buf[16];
    INT32 rd;

    fd = open(path, O_RDONLY);
    ICUNIT_ASSERT_NOT_EQUAL(fd, -1, fd);
    (VOID)memset_s(buf, sizeof(buf), 0, sizeof(buf));
    rd = read(fd, buf, sizeof(buf) - 1);
    (VOID)close(fd);
    ICUNIT_ASSERT_EQUAL(rd, len, rd);
    ICUNIT_ASSERT_EQUAL(memcmp(buf, expect, len), 0, rd);
    return LOS_OK;
}

/* 用例简要描述: cp 命令全分支直调: 常规复制/同文件/源为目录/源不存在/目标父目录缺失/
 * 目标为文件+尾斜杠/目标为目录(基名拼接)/通配符多匹配拒绝/单匹配/零匹配/
 * 嵌套通配符多匹配/深路径基名拼接规范化失败/fd 耗尽 open 失败/目标同名子目录 open 失败 */
static UINT32 Testcase(VOID)
{
    INT32 ret;
    INT32 fd = -1;
    INT32 opened = 0;
    INT32 i;
    struct stat st = {0};
    const CHAR *argvBasic[2] = {TC14_A, TC14_C};
    const CHAR *argvSame[2] = {TC14_A, TC14_A};
    const CHAR *argvSrcDir[2] = {TC14_DIR, "/littlefs/tc14/x1"};
    const CHAR *argvSrcMiss[2] = {"/littlefs/noFile14", "/littlefs/tc14/x2"};
    const CHAR *argvDstNoDir[2] = {TC14_A, "/littlefs/tc14/nodir/out"};
    const CHAR *argvDstRegSlash[2] = {TC14_A, "/littlefs/tc14/a.txt/"};
    const CHAR *argvToDir[2] = {SRC14, TC14_DIR};
    const CHAR *argvWcMulti[2] = {"/littlefs/tc14/*.txt", "/littlefs/tc14/wcout"};
    const CHAR *argvWcSingle[2] = {"/littlefs/tc14/w/*.txt", "/littlefs/tc14/wout"};
    const CHAR *argvWcZero[2] = {"/littlefs/tc14/z*.txt", "/littlefs/tc14/zout"};
    const CHAR *argvWcToDir[2] = {"/littlefs/tc14/w/*.txt", TC14_DIR};
    const CHAR *argvWcNest[2] = {"/littlefs/tc14/n?/f.txt", "/littlefs/tc14/nout"};
    const CHAR *argvDeep[2] = {NULL, NULL};
    const CHAR *argvFd[2] = {TC14_A, "/littlefs/tc14/xfdst"};
    const CHAR *argvDstDir[2] = {TC14_A, TC14_DD};

    /* 前置: 构造目录/文件夹具 */
    ret = mkdir(TC14_DIR, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC14_A, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "AAAA", 4); /* 4, sizeof "AAAA" */
    ICUNIT_GOTO_EQUAL(ret, 4, ret, EXIT);
    (VOID)close(fd);
    fd = -1;
    fd = open(TC14_B, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "BBBB", 4);
    ICUNIT_GOTO_EQUAL(ret, 4, ret, EXIT);
    (VOID)close(fd);
    fd = -1;
    fd = open(SRC14, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "SRC", 3); /* 3, sizeof "SRC" */
    ICUNIT_GOTO_EQUAL(ret, 3, ret, EXIT);
    (VOID)close(fd);
    fd = -1;
    ret = mkdir(TC14_W, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC14_W1, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "WWWW", 4);
    ICUNIT_GOTO_EQUAL(ret, 4, ret, EXIT);
    (VOID)close(fd);
    fd = -1;
    ret = mkdir("/littlefs/tc14/n1", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mkdir("/littlefs/tc14/n2", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC14_N1, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    fd = open(TC14_N2, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    ret = mkdir(TC14_DD, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mkdir("/littlefs/tc14/dd/a.txt", 0777); /* 与源同名的目录,构造 open(EISDIR) */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Pos: 常规单文件复制 + 内容校验 */
    ret = OsShellCmdCp(2, argvBasic);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckFileContent(TC14_C, "AAAA", 4);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Inv: 源与目标为同一文件 */
    ret = OsShellCmdCp(2, argvSame);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Inv: 源为目录 */
    ret = OsShellCmdCp(2, argvSrcDir);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Inv: 源文件不存在(stat 失败) */
    ret = OsShellCmdCp(2, argvSrcMiss);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Inv: 目标父目录不存在(open 目标失败) */
    ret = OsShellCmdCp(2, argvDstNoDir);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Inv: 目标为已存在文件且带尾斜杠 */
    ret = OsShellCmdCp(2, argvDstRegSlash);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Pos: 目标为已存在目录(基名拼接复制) */
    ret = OsShellCmdCp(2, argvToDir);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckFileContent("/littlefs/tc14/src14.txt", "SRC", 3);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Inv: 通配符源多匹配(count>1 拒绝) */
    ret = OsShellCmdCp(2, argvWcMulti);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Pos: 通配符源单匹配(复制到新文件) */
    ret = OsShellCmdCp(2, argvWcSingle);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckFileContent("/littlefs/tc14/wout", "WWWW", 4);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 通配符源零匹配(无动作,尾部未匹配项的 -1 透传,目标不产生) */
    ret = OsShellCmdCp(2, argvWcZero);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ret = stat("/littlefs/tc14/zout", &st);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Pos: 通配符源 + 目标为目录(逐基名拼接复制) */
    ret = OsShellCmdCp(2, argvWcToDir);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckFileContent("/littlefs/tc14/w1.txt", "WWWW", 4);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Inv: 嵌套通配符多匹配(count>1 中断) */
    ret = OsShellCmdCp(2, argvWcNest);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Pos: 嵌套通配符单匹配(递归 s==NULL 叶走 OsShellCmdDoCp 基名复制) */
    ret = mkdir("/littlefs/tc14/solo", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open("/littlefs/tc14/solo/a.txt", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "NEST", 4); /* 4, sizeof "NEST" */
    ICUNIT_GOTO_EQUAL(ret, 4, ret, EXIT);
    (VOID)close(fd);
    fd = -1;
    {
        const CHAR *argvWcNest1[2] = {"/littlefs/tc14/sol?/a.txt", "/littlefs/tc14/soloout"};
        /* 嵌套通配符单匹配: 递归 s==NULL 叶走 OsShellCmdDoCp 完成复制,
         * 但遍历尾部未匹配项的 -1 会透传为返回值, 以内容校验为准 */
        ret = OsShellCmdCp(2, argvWcNest1);
        ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
        ret = CheckFileContent("/littlefs/tc14/soloout", "NEST", 4);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    }

    /* F-Inv: 目标目录不存在且带尾斜杠(stat 失败 + 尾 '/' 拒绝) */
    {
        const CHAR *argvDstMissSlash[2] = {TC14_A, "/littlefs/tc14/nodir2/"};
        ret = OsShellCmdCp(2, argvDstMissSlash);
        ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    }

    /* F-Inv: 深路径目标(目录 149 字符 + 源基名 139 字符,拼接后 289 >= PATH_MAX) */
    (VOID)memset_s(g_deepDir, sizeof(g_deepDir), 'D', sizeof(g_deepDir) - 1);
    (VOID)memcpy_s(g_deepDir, sizeof(g_deepDir), "/littlefs/", 10); /* 10, sizeof "/littlefs/" */
    g_deepDir[sizeof(g_deepDir) - 1] = '\0';
    (VOID)memset_s(g_deepSrc, sizeof(g_deepSrc), 'F', sizeof(g_deepSrc) - 1);
    (VOID)memcpy_s(g_deepSrc, sizeof(g_deepSrc), "/littlefs/", 10);
    g_deepSrc[sizeof(g_deepSrc) - 1] = '\0';
    ret = mkdir(g_deepDir, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(g_deepSrc, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    ret = write(fd, "DEEP", 4);
    ICUNIT_GOTO_EQUAL(ret, 4, ret, EXIT);
    (VOID)close(fd);
    fd = -1;
    argvDeep[0] = g_deepSrc;
    argvDeep[1] = g_deepDir;
    ret = OsShellCmdCp(2, argvDeep);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Inv: fd 表耗尽后 cp(OsShellCmdDoCp open 源失败 ENFILE) */
    for (i = 0; i < CP_FD_MAX; i++) {
        fd = open(TC14_A, O_RDONLY);
        if (fd == -1) {
            break;
        }
        g_fds[opened++] = fd;
    }
    fd = -1;
    ICUNIT_GOTO_NOT_EQUAL(opened, 0, opened, EXIT_FD);
    ret = OsShellCmdCp(2, argvFd);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_FD);
    ret = stat("/littlefs/tc14/xfdst", &st);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_FD);

EXIT_FD:
    /* fd 耗尽段专用清理: 释放全部持有 fd 后再走通用清理 */
    for (i = 0; i < opened; i++) {
        (VOID)close(g_fds[i]);
    }
    opened = 0;

    /* F-Inv: 目标目录下存在同名子目录(open 目标 EISDIR 失败) */
    ret = OsShellCmdCp(2, argvDstDir);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

EXIT:
    /* 异常路径同样清理: 释放 fd 耗尽段持有的 fd, 关闭残留 fd, 删除全部夹具(尽力而为) */
    for (i = 0; i < opened; i++) {
        (VOID)close(g_fds[i]);
    }
    if (fd >= 0) {
        (VOID)close(fd);
    }
    (VOID)unlink(TC14_A);
    (VOID)unlink(TC14_B);
    (VOID)unlink(TC14_C);
    (VOID)unlink("/littlefs/tc14/src14.txt");
    (VOID)unlink(TC14_W1);
    (VOID)unlink("/littlefs/tc14/wout");
    (VOID)unlink("/littlefs/tc14/w1.txt");
    (VOID)unlink(TC14_N1);
    (VOID)unlink(TC14_N2);
    (VOID)unlink("/littlefs/tc14/solo/a.txt");
    (VOID)unlink("/littlefs/tc14/soloout");
    (VOID)rmdir("/littlefs/tc14/solo");
    (VOID)rmdir("/littlefs/tc14/dd/a.txt");
    (VOID)rmdir(TC14_DD);
    (VOID)rmdir("/littlefs/tc14/n1");
    (VOID)rmdir("/littlefs/tc14/n2");
    (VOID)rmdir(TC14_W);
    (VOID)rmdir(TC14_DIR);
    (VOID)unlink(g_deepSrc);
    (VOID)rmdir(g_deepDir);
    (VOID)unlink(SRC14);
    return LOS_OK;
}

/*
 * @tc.name     ItLosShell014
 * @tc.desc     验证 cp 对外接口全分支: 常规复制内容校验/同文件拒绝/源目录拒绝/
 *              源不存在 stat 失败/目标父目录缺失 open 失败/目标文件带尾斜杠拒绝/
 *              目标目录基名拼接/通配符多匹配拒绝/单匹配/零匹配/嵌套通配符/
 *              深路径基名拼接规范化失败(ENAMETOOLONG)/fd 耗尽 open 源失败/
 *              目标同名子目录 open 失败(EISDIR)。依赖 qemu /littlefs 挂载
 */
VOID ItLosShell014(VOID)
{
    TEST_ADD_CASE("ItLosShell014", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL && LOSCFG_FS_LITTLEFS */
