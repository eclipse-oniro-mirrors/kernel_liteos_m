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

#define TC13_DIR    "/littlefs/tc13"
#define TC13_NEWF   "/littlefs/tc13/newf"
#define TC13_BIG    "/littlefs/tc13/big.txt"
#define TC13_NOF    "/littlefs/tc13/nodir/f"
#define CAT_TEST_LEN 600 /* > CAT_BUF_SIZE-1(511),触发 cat 多轮读取 */

/* 用例简要描述: touch/cat/mkdir 成功与失败路径直调: mkdir 创建/已存在(perror),
 * touch 创建/已存在/父目录缺失, cat 空文件/大文件(多轮 512B 读循环)/不存在文件 */
static UINT32 Testcase(VOID)
{
    INT32 ret;
    INT32 fd = -1;
    struct stat st = {0};
    static CHAR bigBuf[CAT_TEST_LEN];
    const CHAR *argvDir[1] = {TC13_DIR};
    const CHAR *argvNewf[1] = {TC13_NEWF};
    const CHAR *argvBig[1] = {TC13_BIG};
    const CHAR *argvNoF[1] = {TC13_NOF};

    /* F-Pos: mkdir 创建目录 */
    ret = OsShellCmdMkdir(1, argvDir);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = stat(TC13_DIR, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(S_ISDIR(st.st_mode), 1, 0, EXIT);

    /* F-Pos: mkdir 已存在目录(mkdir 失败 perror,返回 0) */
    ret = OsShellCmdMkdir(1, argvDir);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Pos: touch 创建新文件 */
    ret = OsShellCmdTouch(1, argvNewf);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = stat(TC13_NEWF, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(S_ISREG(st.st_mode), 1, 0, EXIT);
    ICUNIT_GOTO_EQUAL(st.st_size, 0, st.st_size, EXIT);

    /* F-Pos: touch 已存在文件(open 复用) */
    ret = OsShellCmdTouch(1, argvNewf);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Inv: touch 父目录不存在(open O_CREAT 失败) */
    ret = OsShellCmdTouch(1, argvNoF);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

    /* F-Pos: cat 空文件(read 返回 0,单轮退出) */
    ret = OsShellCmdCat(1, argvNewf);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 前置: 构造 600 字节文件(> CAT_BUF_SIZE-1,触发多轮读) */
    fd = open(TC13_BIG, O_CREAT | O_RDWR, 0666);
    ICUNIT_GOTO_NOT_EQUAL(fd, -1, fd, EXIT);
    (VOID)memset_s(bigBuf, sizeof(bigBuf), 'B', sizeof(bigBuf));
    ret = write(fd, bigBuf, sizeof(bigBuf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(bigBuf), ret, EXIT);
    (VOID)close(fd);
    fd = -1;

    /* F-Pos: cat 大文件(两轮 read 循环 + LOS_TaskDelay) */
    ret = OsShellCmdCat(1, argvBig);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* F-Inv: cat 不存在文件 */
    ret = OsShellCmdCat(1, argvNoF);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);

EXIT:
    /* 异常路径同样清理: 关闭残留 fd,删除夹具(尽力而为) */
    if (fd >= 0) {
        (VOID)close(fd);
    }
    (VOID)unlink(TC13_BIG);
    (VOID)unlink(TC13_NEWF);
    (VOID)rmdir(TC13_DIR);
    return LOS_OK;
}

/*
 * @tc.name     ItLosShell013
 * @tc.desc     验证 touch/cat/mkdir 对外接口: mkdir 创建与已存在(perror 路径),
 *              touch 创建/重复创建/父目录缺失, cat 空文件单轮读、600 字节文件
 *              多轮读循环、不存在文件失败。依赖 qemu /littlefs 挂载
 */
VOID ItLosShell013(VOID)
{
    TEST_ADD_CASE("ItLosShell013", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL && LOSCFG_FS_LITTLEFS */
