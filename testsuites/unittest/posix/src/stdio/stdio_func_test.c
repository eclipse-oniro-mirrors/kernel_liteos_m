/*
 * Copyright (c) 2023-2023 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder, nor the name of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* stdio 域接口用例：feof/fgetc/fgets/fputc/perror/ungetc
 *
 * 可行性依据（third_party/musl/porting/liteos_m/kernel/src/stdio/）：
 * - fopen.c:23 直接调内核 open()，__fdopen.c:50-53 挂 __stdio_read/__stdio_write/
 *   __stdio_seek/__stdio_close 全套内核 syscall，stdio 文件族可用真实文件流测试；
 * - __stdio_read.c:13 read(f->fd,...)、__stdio_write.c:15-16 write(f->fd,...)；
 * - perror.c:23-24 经 stderr（stderr.c: fd=2, __stdio_write 直写控制台）输出，
 *   不依赖文件系统挂载；
 * - 文件流用例依赖 TEST_ROOT 挂载点（同 posix_fs_test.h 的取值），仅在
 *   LOSCFG_SUPPORT_LITTLEFS/LOSCFG_SUPPORT_FATFS 打开的产品上编译执行，
 *   与 posix_test.c:80 对 PosixFsFuncTest 的门控方式保持一致。
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "ohos_types.h"
#include "posix_test.h"
#include "los_config.h"
#include "kernel_test.h"
#include "log.h"

#if (LOSCFG_SUPPORT_FATFS == 1)
#define STDIO_TEST_ROOT "system"
#endif

#if (LOSCFG_SUPPORT_LITTLEFS == 1)
#define STDIO_TEST_ROOT "/littlefs"
#endif

#define STDIO_TEST_FILE STDIO_TEST_ROOT"/STDIO_F0"

/* *
 * @tc.desc      : register a test suite, this suite is used to test basic flow and interface dependency
 * @param        : subsystem name is utils
 * @param        : module name is utilsFile
 * @param        : test suit name is PosixStdioFuncTestSuite
 */
LITE_TEST_SUIT(Posix, PosixStdio, PosixStdioFuncTestSuite);

/* *
 * @tc.setup     : setup for all testcases
 * @return       : setup result, TRUE is success, FALSE is fail
 */
static BOOL PosixStdioFuncTestSuiteSetUp(void)
{
    return TRUE;
}

/* *
 * @tc.teardown  : teardown for all testcases
 * @return       : teardown result, TRUE is success, FALSE is fail
 */
static BOOL PosixStdioFuncTestSuiteTearDown(void)
{
    LOG("+-------------------------------------------+\n");
    return TRUE;
}

/* *
 * @tc.number    : TEST_STDIO_PERROR_001
 * @tc.name      : perror smoke test, output to stderr and keep errno unchanged
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 perror.c:18 的 msg 非空/NULL/空前缀三分支与 stderr 直写路径；
 * perror 实现仅读取 errno（strerror(errno)）不写 errno，stderr fd=2 直写
 * 控制台成功时不改变 errno，故断言调用前后 errno 保持不变。
 */
LITE_TEST_CASE(PosixStdioFuncTestSuite, testStdioPerror001, Function | MediumTest | Level1)
{
    errno = ENOENT;
    perror("PosixStdio perror smoke prefix");
    ICUNIT_ASSERT_EQUAL(errno, ENOENT, errno);

    /* msg 为 NULL：跳过前缀分支（perror.c:18），仅输出错误描述 */
    errno = EINVAL;
    perror(NULL);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* msg 为空串：*msg == 0 走同一跳过分支 */
    perror("");
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);
    return 0;
}

/* *
 * @tc.number    : TEST_STDIO_FOPEN_001
 * @tc.name      : fopen rejects invalid mode with EINVAL before touching the path
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 fopen.c:15-18 的 mode 防御分支：mode 为 NULL 或首字符非 r/w/a 时
 * 在调用 open 之前直接返回 NULL 并置 EINVAL，不依赖文件系统是否挂载。
 */
LITE_TEST_CASE(PosixStdioFuncTestSuite, testStdioFopenParam001, Function | MediumTest | Level1)
{
    FILE *fp = NULL;

    errno = 0;
    fp = fopen("/no_such_path/stdio_fopen", NULL);
    ICUNIT_ASSERT_EQUAL(fp, NULL, 0);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    errno = 0;
    fp = fopen("/no_such_path/stdio_fopen", "z");
    ICUNIT_ASSERT_EQUAL(fp, NULL, 0);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);
    return 0;
}

#ifdef STDIO_TEST_ROOT

/* *
 * @tc.number    : TEST_STDIO_FPUTC_FGETC_001
 * @tc.name      : fputc writes known bytes and fgetc reads them back after fflush/rewind
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 fputc.c do_putc 写路径（__stdio_write→write）与 fgetc.c do_getc
 * 读路径（__stdio_read→read）的往返；fputc 返回值应为写入字符；
 * w+ 模式下写转读须经 fflush + rewind 定位。
 */
LITE_TEST_CASE(PosixStdioFuncTestSuite, testStdioFputcFgetc001, Function | MediumTest | Level1)
{
    FILE *fp = NULL;
    int ret;
    int c;

    (void)remove(STDIO_TEST_FILE);
    fp = fopen(STDIO_TEST_FILE, "w+");
    ICUNIT_GOTO_NOT_EQUAL(fp, NULL, 0, EXIT);

    ret = fputc('H', fp);
    ICUNIT_GOTO_EQUAL(ret, 'H', ret, EXIT);
    ret = fputc('i', fp);
    ICUNIT_GOTO_EQUAL(ret, 'i', ret, EXIT);

    ret = fflush(fp);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    rewind(fp);
    c = fgetc(fp);
    ICUNIT_GOTO_EQUAL(c, 'H', c, EXIT);
    c = fgetc(fp);
    ICUNIT_GOTO_EQUAL(c, 'i', c, EXIT);

    ret = fclose(fp);
    fp = NULL;
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = remove(STDIO_TEST_FILE);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return 0;
EXIT:
    if (fp != NULL) {
        (void)fclose(fp);
    }
    (void)remove(STDIO_TEST_FILE);
    return 0;
}

/* *
 * @tc.number    : TEST_STDIO_FGETC_EOF_001
 * @tc.name      : fgetc returns EOF past end and feof becomes true
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 __stdio_read.c:15-18 read 返回 0 时置 F_EOF 的分支、feof.c:8 对
 * F_EOF 的检查，以及 __toread.c:13 在 F_EOF 已置位时不再触发 read 的短路分支。
 */
LITE_TEST_CASE(PosixStdioFuncTestSuite, testStdioFgetcEof001, Function | MediumTest | Level1)
{
    FILE *fp = NULL;
    int c;

    (void)remove(STDIO_TEST_FILE);
    fp = fopen(STDIO_TEST_FILE, "w+");
    ICUNIT_GOTO_NOT_EQUAL(fp, NULL, 0, EXIT);
    ICUNIT_GOTO_EQUAL(fputc('A', fp), 'A', 'A', EXIT);
    ICUNIT_GOTO_EQUAL(fflush(fp), 0, 0, EXIT);

    rewind(fp);
    ICUNIT_GOTO_EQUAL(feof(fp), 0, 0, EXIT);

    c = fgetc(fp);
    ICUNIT_GOTO_EQUAL(c, 'A', c, EXIT);
    /* 越读：read 返回 0 置 F_EOF，fgetc 返回 EOF */
    c = fgetc(fp);
    ICUNIT_GOTO_EQUAL(c, EOF, c, EXIT);
    ICUNIT_GOTO_EQUAL(feof(fp), 1, 1, EXIT);

    /* F_EOF 已置位时再次 fgetc 走 __toread 短路分支，仍返回 EOF */
    c = fgetc(fp);
    ICUNIT_GOTO_EQUAL(c, EOF, c, EXIT);
    ICUNIT_GOTO_EQUAL(feof(fp), 1, 1, EXIT);

    ICUNIT_GOTO_EQUAL(fclose(fp), 0, 0, EXIT);
    fp = NULL;
    ICUNIT_GOTO_EQUAL(remove(STDIO_TEST_FILE), 0, 0, EXIT);
    return 0;
EXIT:
    if (fp != NULL) {
        (void)fclose(fp);
    }
    (void)remove(STDIO_TEST_FILE);
    return 0;
}

/* *
 * @tc.number    : TEST_STDIO_FGETS_001
 * @tc.name      : fgets reads newline-terminated line, EOF short line and NULL at EOF
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 fgets.c:25-32 缓冲内 memchr 找 '\n' 的按行截断分支、:34-36 读到
 * EOF 时若已有内容（短行）返回缓冲、无内容（p==s 且 feof）返回 NULL 的分支。
 * 文件内容 "hello\nworld"：第二行无换行符，以 EOF 结束。
 */
LITE_TEST_CASE(PosixStdioFuncTestSuite, testStdioFgets001, Function | MediumTest | Level1)
{
    FILE *fp = NULL;
    char buf[64];
    const char *text = "hello\nworld";
    char *ret;

    (void)remove(STDIO_TEST_FILE);
    fp = fopen(STDIO_TEST_FILE, "w+");
    ICUNIT_GOTO_NOT_EQUAL(fp, NULL, 0, EXIT);
    for (const char *p = text; *p != '\0'; p++) {
        ICUNIT_GOTO_EQUAL(fputc(*p, fp), (int)(unsigned char)*p, *p, EXIT);
    }
    ICUNIT_GOTO_EQUAL(fflush(fp), 0, 0, EXIT);

    rewind(fp);
    ret = fgets(buf, sizeof(buf), fp);
    ICUNIT_GOTO_NOT_EQUAL(ret, NULL, 0, EXIT);
    ICUNIT_GOTO_EQUAL(strcmp(buf, "hello\n"), 0, 0, EXIT);

    /* 第二行无换行符：读到 EOF 停止，返回缓冲内容 "world" */
    ret = fgets(buf, sizeof(buf), fp);
    ICUNIT_GOTO_NOT_EQUAL(ret, NULL, 0, EXIT);
    ICUNIT_GOTO_EQUAL(strcmp(buf, "world"), 0, 0, EXIT);

    /* 再读已处于 EOF 且无新内容：返回 NULL */
    ret = fgets(buf, sizeof(buf), fp);
    ICUNIT_GOTO_EQUAL(ret, NULL, 0, EXIT);

    ICUNIT_GOTO_EQUAL(fclose(fp), 0, 0, EXIT);
    fp = NULL;
    ICUNIT_GOTO_EQUAL(remove(STDIO_TEST_FILE), 0, 0, EXIT);
    return 0;
EXIT:
    if (fp != NULL) {
        (void)fclose(fp);
    }
    (void)remove(STDIO_TEST_FILE);
    return 0;
}

/* *
 * @tc.number    : TEST_STDIO_FGETS_002
 * @tc.name      : fgets parameter guard: n<=0 returns NULL and n==1 returns empty string without consuming
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 fgets.c:15-21 的 n 防御分支：n 为 0 时返回 NULL（n--<=1 后 n<0）；
 * n 为 1 时返回空缓冲且不消费流内容，随后的 fgetc 仍读到首字节。
 */
LITE_TEST_CASE(PosixStdioFuncTestSuite, testStdioFgetsParam001, Function | MediumTest | Level1)
{
    FILE *fp = NULL;
    char buf[8];
    char *ret;

    (void)remove(STDIO_TEST_FILE);
    fp = fopen(STDIO_TEST_FILE, "w+");
    ICUNIT_GOTO_NOT_EQUAL(fp, NULL, 0, EXIT);
    ICUNIT_GOTO_EQUAL(fputc('A', fp), 'A', 'A', EXIT);
    ICUNIT_GOTO_EQUAL(fputc('B', fp), 'B', 'B', EXIT);
    ICUNIT_GOTO_EQUAL(fflush(fp), 0, 0, EXIT);
    rewind(fp);

    ret = fgets(buf, 0, fp);
    ICUNIT_GOTO_EQUAL(ret, NULL, 0, EXIT);

    ret = fgets(buf, 1, fp);
    ICUNIT_GOTO_NOT_EQUAL(ret, NULL, 0, EXIT);
    ICUNIT_GOTO_EQUAL(buf[0], '\0', buf[0], EXIT);

    /* n==1 分支不消费流：下一个 fgetc 仍读到首字节 'A' */
    ICUNIT_GOTO_EQUAL(fgetc(fp), 'A', 'A', EXIT);

    ICUNIT_GOTO_EQUAL(fclose(fp), 0, 0, EXIT);
    fp = NULL;
    ICUNIT_GOTO_EQUAL(remove(STDIO_TEST_FILE), 0, 0, EXIT);
    return 0;
EXIT:
    if (fp != NULL) {
        (void)fclose(fp);
    }
    (void)remove(STDIO_TEST_FILE);
    return 0;
}

/* *
 * @tc.number    : TEST_STDIO_UNGETC_001
 * @tc.name      : ungetc pushes back one character which the next read returns
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 ungetc.c:9-19 的回退分支（rpos 回退写入、返回入参字符）。POSIX 仅
 * 保证 1 字节 pushback：读出 'A' 后 ungetc('A')，下一次读得到 'A'，随后
 * 继续读到流中后续内容 'B'。musl 实现为覆盖缓冲回退位（ungetc.c:15
 * *--f->rpos = c），故再验证不同字符回退时 pushback 字符优先于后续流内容。
 */
LITE_TEST_CASE(PosixStdioFuncTestSuite, testStdioUngetc001, Function | MediumTest | Level1)
{
    FILE *fp = NULL;
    int c;

    (void)remove(STDIO_TEST_FILE);
    fp = fopen(STDIO_TEST_FILE, "w+");
    ICUNIT_GOTO_NOT_EQUAL(fp, NULL, 0, EXIT);
    ICUNIT_GOTO_EQUAL(fputc('A', fp), 'A', 'A', EXIT);
    ICUNIT_GOTO_EQUAL(fputc('B', fp), 'B', 'B', EXIT);
    ICUNIT_GOTO_EQUAL(fflush(fp), 0, 0, EXIT);

    rewind(fp);
    c = fgetc(fp);
    ICUNIT_GOTO_EQUAL(c, 'A', c, EXIT);

    c = ungetc('A', fp);
    ICUNIT_GOTO_EQUAL(c, 'A', c, EXIT);
    c = fgetc(fp);
    ICUNIT_GOTO_EQUAL(c, 'A', c, EXIT);
    c = fgetc(fp);
    ICUNIT_GOTO_EQUAL(c, 'B', c, EXIT);

    /* 不同字符回退：pushback 字符被下一次读优先返回（musl 覆盖式实现） */
    rewind(fp);
    ICUNIT_GOTO_EQUAL(fgetc(fp), 'A', 'A', EXIT);
    c = ungetc('Z', fp);
    ICUNIT_GOTO_EQUAL(c, 'Z', c, EXIT);
    c = fgetc(fp);
    ICUNIT_GOTO_EQUAL(c, 'Z', c, EXIT);

    ICUNIT_GOTO_EQUAL(fclose(fp), 0, 0, EXIT);
    fp = NULL;
    ICUNIT_GOTO_EQUAL(remove(STDIO_TEST_FILE), 0, 0, EXIT);
    return 0;
EXIT:
    if (fp != NULL) {
        (void)fclose(fp);
    }
    (void)remove(STDIO_TEST_FILE);
    return 0;
}

/* *
 * @tc.number    : TEST_STDIO_UNGETC_002
 * @tc.name      : ungetc(EOF) is rejected and ungetc clears the EOF flag
 * @tc.desc      : [C- SOFTWARE -0200]
 * 覆盖 ungetc.c:5 的 c==EOF 防御分支（返回 EOF 且不移动流位置），以及
 * ungetc.c:16 的 flags &= ~F_EOF 分支：EOF 状态下 ungetc 后 feof 归零、
 * 可继续读出 pushback 字符，再次越读后 F_EOF 重新置位。
 */
LITE_TEST_CASE(PosixStdioFuncTestSuite, testStdioUngetcEof001, Function | MediumTest | Level1)
{
    FILE *fp = NULL;
    int c;

    (void)remove(STDIO_TEST_FILE);
    fp = fopen(STDIO_TEST_FILE, "w+");
    ICUNIT_GOTO_NOT_EQUAL(fp, NULL, 0, EXIT);
    ICUNIT_GOTO_EQUAL(fputc('A', fp), 'A', 'A', EXIT);
    ICUNIT_GOTO_EQUAL(fflush(fp), 0, 0, EXIT);

    rewind(fp);
    ICUNIT_GOTO_EQUAL(fgetc(fp), 'A', 'A', EXIT);
    ICUNIT_GOTO_EQUAL(fgetc(fp), EOF, EOF, EXIT);
    ICUNIT_GOTO_EQUAL(feof(fp), 1, 1, EXIT);

    /* c==EOF 防御分支：返回 EOF，流位置与 EOF 状态不变 */
    c = ungetc(EOF, fp);
    ICUNIT_GOTO_EQUAL(c, EOF, c, EXIT);
    ICUNIT_GOTO_EQUAL(fgetc(fp), EOF, EOF, EXIT);
    ICUNIT_GOTO_EQUAL(feof(fp), 1, 1, EXIT);

    /* 有效回退清除 F_EOF：feof 归零并可读出回退字符 */
    c = ungetc('z', fp);
    ICUNIT_GOTO_EQUAL(c, 'z', c, EXIT);
    ICUNIT_GOTO_EQUAL(feof(fp), 0, 0, EXIT);
    ICUNIT_GOTO_EQUAL(fgetc(fp), 'z', 'z', EXIT);
    ICUNIT_GOTO_EQUAL(fgetc(fp), EOF, EOF, EXIT);
    ICUNIT_GOTO_EQUAL(feof(fp), 1, 1, EXIT);

    ICUNIT_GOTO_EQUAL(fclose(fp), 0, 0, EXIT);
    fp = NULL;
    ICUNIT_GOTO_EQUAL(remove(STDIO_TEST_FILE), 0, 0, EXIT);
    return 0;
EXIT:
    if (fp != NULL) {
        (void)fclose(fp);
    }
    (void)remove(STDIO_TEST_FILE);
    return 0;
}

#endif /* STDIO_TEST_ROOT */

RUN_TEST_SUITE(PosixStdioFuncTestSuite);

void PosixStdioFuncTest(void)
{
    LOG("begin PosixStdioFuncTest....");
    /*
     * 文件流用例（STDIO_TEST_ROOT 门控段）需要 TEST_ROOT 可写挂载；板级按需
     * 挂卷（弱默认=启动时已挂载，如 qemu），失败则跳过本套件
     * （XTS_FS_RAMDISK_PLAN.md：hi3322_testsuits 由 ram_lfs_backend.c
     * 提供 RAM 盘强符号）。
     */
    if (TestFsVolumeEnsure() != 0) {
        LOG("PosixStdioFuncTest: TEST_ROOT volume unavailable, skip\n");
        return;
    }
    RUN_ONE_TESTCASE(testStdioPerror001);
    RUN_ONE_TESTCASE(testStdioFopenParam001);
#ifdef STDIO_TEST_ROOT
    RUN_ONE_TESTCASE(testStdioFputcFgetc001);
    RUN_ONE_TESTCASE(testStdioFgetcEof001);
    RUN_ONE_TESTCASE(testStdioFgets001);
    RUN_ONE_TESTCASE(testStdioFgetsParam001);
    RUN_ONE_TESTCASE(testStdioUngetc001);
    RUN_ONE_TESTCASE(testStdioUngetcEof001);
#endif
    (void)TestFsVolumeRelease();

    return;
}
