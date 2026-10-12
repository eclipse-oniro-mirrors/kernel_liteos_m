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
 * 3. Neither the name of the copyright holder nor its names of contributors may be used
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
#include <dirent.h>

#if defined(LOSCFG_SHELL) && defined(LOSCFG_FS_LITTLEFS)

#define TC15_BASE  "/littlefs/tc15"
#define TC15_F1    "/littlefs/tc15/f1"
#define TC15_D1    "/littlefs/tc15/d1"
#define TC15_QDIR  "/littlefs/tc15/qdir"
#define DIR_HOLE_MAX 15 /* LOSCFG_MAX_OPEN_DIRS(10)+余量 */

static CHAR g_deepA[211]; /* "/littlefs/" + 200 字符名 = 210 字符路径 */
static CHAR g_deepC[256]; /* 210 + 1 + 44 字符名 = 255 字符路径(极限) */
static CHAR g_deepA2[211];
static CHAR g_deepC2[255]; /* 210 + 1 + 43 字符名 = 254 字符路径(次极限) */

/* 断言路径存在性: exist=1 应存在, exist=0 应不存在(辅助函数无资源,失败直接返回) */
static UINT32 CheckPathExist(const CHAR *path, INT32 exist)
{
    struct stat st = {0};
    INT32 ret = stat(path, &st);
    if (exist == 1) {
        ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    } else {
        ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    }
    return LOS_OK;
}

/* 用例简要描述: rm/rmdir 全分支直调: 单文件/目录上 rm 拒绝/rm -r 文件与递归目录树/
 * rmdir 空与非空目录/通配符 RM_FILE、RM_DIR、RM_RECURSIVER/通配符匹配矩阵
 * ('*' '?' 精确/重试/尾星/无匹配)/根通配符/递归 s==NULL 叶/深路径 strcat 失败
 * 中止/目录句柄耗尽 opendir 失败/非空目录 RM_DIR 失败打印 */
static UINT32 Testcase(VOID)
{
    INT32 ret;
    INT32 fd = -1;
    INT32 i;
    INT32 opened = 0;
    DIR *dirs[DIR_HOLE_MAX];
    const CHAR *argvF1[1] = {TC15_F1};
    const CHAR *argvD1[1] = {TC15_D1};
    const CHAR *argvRrR[2] = {"-r", TC15_BASE "/rr"};
    const CHAR *argvRrF1[2] = {"-r", TC15_F1};
    const CHAR *argvRrMiss[2] = {"-r", "/littlefs/tc15/no_such_015"};
    const CHAR *argvE[1] = {TC15_BASE "/e*"};
    const CHAR *argvG[1] = {TC15_BASE "/g*"};
    const CHAR *argvH[2] = {"-r", TC15_BASE "/h1*"};
    const CHAR *argvAbx[1] = {TC15_BASE "/m/ab?x"};
    const CHAR *argvAbStar[1] = {TC15_BASE "/m/ab*"};
    const CHAR *argvAq[1] = {TC15_BASE "/m/a?c"};
    const CHAR *argvAc[1] = {TC15_BASE "/m/a*c"};
    const CHAR *argvQqzz[1] = {TC15_BASE "/m/qq*zz"};
    const CHAR *argvStar[1] = {TC15_BASE "/m/*"};
    const CHAR *argvRoot[1] = {"/*zzq"};
    const CHAR *argvPdf[1] = {TC15_BASE "/p/d?/f"};
    const CHAR *argvRmQdir[2] = {"-r", TC15_QDIR};
    const CHAR *argvNe[1] = {TC15_BASE "/ne*"};
    const CHAR *argvRmdirE1[1] = {TC15_BASE "/e1"};

    /* 前置: 构造目录树与文件夹具 */
    ret = mkdir(TC15_BASE, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC15_F1, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    ret = mkdir(TC15_D1, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mkdir(TC15_BASE "/rr", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mkdir(TC15_BASE "/rr/a", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mkdir(TC15_BASE "/rr/a/b", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC15_BASE "/rr/a/b/f", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    ret = mkdir(TC15_BASE "/e1", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mkdir(TC15_BASE "/e2", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC15_BASE "/g1.txt", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    fd = open(TC15_BASE "/g2.txt", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    ret = mkdir(TC15_BASE "/h1", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC15_BASE "/h1/in", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    ret = mkdir(TC15_BASE "/m", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC15_BASE "/m/abc", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    fd = open(TC15_BASE "/m/acabc", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    fd = open(TC15_BASE "/m/ab", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    fd = open(TC15_BASE "/m/a1c", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    fd = open(TC15_BASE "/m/qqq", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    ret = mkdir(TC15_BASE "/p", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mkdir(TC15_BASE "/p/d1", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC15_BASE "/p/d1/f", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    ret = mkdir(TC15_QDIR, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = mkdir(TC15_BASE "/ne1", 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = open(TC15_BASE "/ne1/x", O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;

    /* F-Pos: rm 删除单文件 */
    ret = OsShellCmdRm(1, argvF1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_F1, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Inv: rm 不带 -r 删目录(Is a directory 拒绝) */
    ret = OsShellCmdRm(1, argvD1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_D1, 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: rm -r 删除文件(S_ISREG 走 remove) */
    fd = open(TC15_F1, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)close(fd);
    fd = -1;
    ret = OsShellCmdRm(2, argvRrF1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_F1, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: rm -r 递归删除非空目录树 */
    ret = OsShellCmdRm(2, argvRrR);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/rr", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: rm -r 不存在路径(stat 失败) */
    ret = OsShellCmdRm(2, argvRrMiss);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Pos: rmdir 删除空目录 */
    ret = OsShellCmdRmdir(1, argvRmdirE1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/e1", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: rmdir 非空目录(打印失败,目录保留) */
    ret = OsShellCmdRmdir(1, argvNe);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/ne1", 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: rmdir 通配符删除多个空目录(RM_DIR) */
    ret = OsShellCmdRmdir(1, argvE);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/e2", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: rm 通配符删除多个文件(RM_FILE) */
    ret = OsShellCmdRm(1, argvG);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/g1.txt", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/g2.txt", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: rm -r 通配符递归删除目录(RM_RECURSIVER) */
    ret = OsShellCmdRm(2, argvH);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/h1", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Inv: 通配符无匹配(src 扫描耗尽,文件保留) */
    ret = OsShellCmdRm(1, argvAbx);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/m/ab", 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = OsShellCmdRm(1, argvQqzz);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/m/qqq", 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 尾部 '*' 匹配 + '?' 单字符匹配 + '*' 中缀直接/重试匹配 */
    ret = OsShellCmdRm(1, argvAbStar);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/m/ab", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = OsShellCmdRm(1, argvAq);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/m/a1c", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = OsShellCmdRm(1, argvAc);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/m/abc", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/m/acabc", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 纯 '*' 全匹配 */
    ret = OsShellCmdRm(1, argvStar);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/m/qqq", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 根通配符(目录段为空,f=='' 走 opendir("/")),无匹配不误删 */
    ret = OsShellCmdRm(1, argvRoot);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist("/littlefs", 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 多级通配符,尾部为字面名(递归 s==NULL 叶直接删除) */
    ret = OsShellCmdRm(1, argvPdf);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/p/d1/f", 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = CheckPathExist(TC15_BASE "/p/d1", 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Inv: 目录句柄耗尽后 rm -r(DoRmdir opendir ENFILE 失败,目录保留) */
    for (i = 0; i < DIR_HOLE_MAX; i++) {
        dirs[i] = opendir(TC15_BASE);
        if (dirs[i] == NULL) {
            break;
        }
        opened++;
    }
    ICUNIT_GOTO_NOT_EQUAL(opened, DIR_HOLE_MAX, opened, EXIT);
    ret = OsShellCmdRm(2, argvRmQdir);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CheckPathExist(TC15_QDIR, 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    for (i = 0; i < opened; i++) {
        (VOID)closedir(dirs[i]);
    }
    opened = 0;

    /* F-Inv: 深路径通配符(匹配项全路径 255 字符,拼接 t 段 strcat_s 失败中止) */
    (VOID)memset_s(g_deepA, sizeof(g_deepA), 'A', sizeof(g_deepA) - 1);
    (VOID)memcpy_s(g_deepA, sizeof(g_deepA), "/littlefs/", 10); /* 10, sizeof "/littlefs/" */
    g_deepA[sizeof(g_deepA) - 1] = '\0'; /* 210 字符路径 */
    ret = mkdir(g_deepA, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    /* 构造 255 字符极限路径: 210 字符目录 + '/' + 44 字符子目录名 */
    (VOID)memcpy_s(g_deepC, sizeof(g_deepC), g_deepA, sizeof(g_deepA)); /* 含结尾 '\0' */
    g_deepC[sizeof(g_deepA) - 1] = '/';
    (VOID)memset_s(&g_deepC[sizeof(g_deepA)], sizeof(g_deepC) - sizeof(g_deepA), 'C',
                   sizeof(g_deepC) - sizeof(g_deepA) - 1); /* 44 个 'C' */
    g_deepC[255] = '\0';
    ret = mkdir(g_deepC, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    {
        CHAR deepPattern[220];
        const CHAR *argvDeepRm[2] = {"-r", deepPattern};
        ret = snprintf_s(deepPattern, sizeof(deepPattern), sizeof(deepPattern) - 1, "%s/*/*", g_deepA);
        ICUNIT_GOTO_NOT_EQUAL(ret, -1, ret, EXIT);
        ret = OsShellCmdRm(2, argvDeepRm);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    ret = CheckPathExist(g_deepC, 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = CheckPathExist(g_deepA, 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Inv: 254 字符次极限路径(分隔符 strcat 成功后, t 段 strcat 失败中止) */
    (VOID)memset_s(g_deepA2, sizeof(g_deepA2), 'B', sizeof(g_deepA2) - 1);
    (VOID)memcpy_s(g_deepA2, sizeof(g_deepA2), "/littlefs/", 10); /* 10, sizeof "/littlefs/" */
    g_deepA2[sizeof(g_deepA2) - 1] = '\0'; /* 210 字符路径, 用 'B' 与前段区分 */
    ret = mkdir(g_deepA2, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (VOID)memcpy_s(g_deepC2, sizeof(g_deepC2), g_deepA2, sizeof(g_deepA2)); /* 含结尾 '\0' */
    g_deepC2[sizeof(g_deepA2) - 1] = '/';
    (VOID)memset_s(&g_deepC2[sizeof(g_deepA2)], sizeof(g_deepC2) - sizeof(g_deepA2), 'C',
                   sizeof(g_deepC2) - sizeof(g_deepA2) - 1); /* 43 个 'C' */
    g_deepC2[254] = '\0'; /* 210+1+43 = 254 字符路径 */
    ret = mkdir(g_deepC2, 0777);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    {
        CHAR deepPattern2[220];
        const CHAR *argvDeepRm2[2] = {"-r", deepPattern2};
        ret = snprintf_s(deepPattern2, sizeof(deepPattern2), sizeof(deepPattern2) - 1, "%s/*/*", g_deepA2);
        ICUNIT_GOTO_NOT_EQUAL(ret, -1, ret, EXIT);
        ret = OsShellCmdRm(2, argvDeepRm2);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    ret = CheckPathExist(g_deepC2, 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = CheckPathExist(g_deepA2, 1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    /* 异常路径同样清理: 释放目录句柄耗尽段持有的 DIR, 关闭残留 fd, 删除全部夹具(尽力而为) */
    for (i = 0; i < opened; i++) {
        (VOID)closedir(dirs[i]);
    }
    if (fd >= 0) {
        (VOID)close(fd);
    }
    (VOID)unlink(TC15_BASE "/ne1/x");
    (VOID)rmdir(TC15_BASE "/ne1");
    (VOID)rmdir(TC15_QDIR);
    (VOID)rmdir(TC15_BASE "/p/d1");
    (VOID)rmdir(TC15_BASE "/p");
    (VOID)rmdir(TC15_BASE "/m");
    (VOID)rmdir(TC15_D1);
    (VOID)rmdir(g_deepC);
    (VOID)rmdir(g_deepA);
    (VOID)rmdir(g_deepC2);
    (VOID)rmdir(g_deepA2);
    (VOID)rmdir(TC15_BASE);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell015
 * @tc.desc     验证 rm/rmdir 对外接口全分支: 单文件删除/目录无 -r 拒绝/rm -r 文件
 *              与递归树/rmdir 空与非空目录/通配符 RM_FILE、RM_DIR、RM_RECURSIVER/
 *              通配符匹配矩阵('?' 单字符、'*' 中缀直接与重试、尾星、无匹配)/
 *              根通配符目录段为空/多级通配符字面尾递归叶/目录句柄耗尽 opendir 失败/
 *              255 字符极限路径通配符 strcat 失败中止。依赖 qemu /littlefs 挂载
 */
VOID ItLosShell015(VOID)
{
    TEST_ADD_CASE("ItLosShell015", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL && LOSCFG_FS_LITTLEFS */
