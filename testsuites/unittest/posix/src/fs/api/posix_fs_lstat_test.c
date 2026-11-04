/*
 * Copyright (c) 2023-2023 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor its contributors may be used
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

#include "posix_fs_test.h"

/* lstat 声明不在适配头文件中, 参考 posix_fs_func_test.c 中 lseek 的 extern 声明方式 */
extern int lstat(const char *path, struct stat *buffer);

/* *
 * @tc.number   SUB_KERNEL_FS_LSTAT_OK
 * @tc.name     lstat
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLstatOK, Function | MediumTest | Level1)
{
    int32_t ret;
    ssize_t size;
    struct stat lstatBuf = { 0 };
    struct stat statBuf = { 0 };
    const char tmpFileName[TEST_BUF_SIZE] = { FILE1 };
    char writeBuf[TEST_BUF_SIZE] = "lstat test";

    (void)remove(tmpFileName);
    int32_t fd = open(tmpFileName, O_CREAT | O_RDWR, TEST_MODE_HIGH);
    ICUNIT_ASSERT_NOT_EQUAL(fd, POSIX_FS_IS_ERROR, fd);

    size = write(fd, writeBuf, sizeof(writeBuf));
    ret = close(fd);
    fd = POSIX_FS_IS_ERROR;
    ICUNIT_GOTO_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);
    ICUNIT_GOTO_NOT_EQUAL(size, POSIX_FS_IS_ERROR, size, EXIT);

    /* lstat 对普通文件等价于 stat 语义, 两者结果应一致 */
    ret = lstat(tmpFileName, &lstatBuf);
    ICUNIT_GOTO_EQUAL(ret, POSIX_FS_NO_ERROR, ret, EXIT);

    ret = stat(tmpFileName, &statBuf);
    ICUNIT_GOTO_EQUAL(ret, POSIX_FS_NO_ERROR, ret, EXIT);

    ICUNIT_GOTO_EQUAL(lstatBuf.st_size, sizeof(writeBuf), lstatBuf.st_size, EXIT);
    ICUNIT_GOTO_EQUAL(lstatBuf.st_size, statBuf.st_size, statBuf.st_size, EXIT);
    ICUNIT_GOTO_EQUAL(lstatBuf.st_mode, statBuf.st_mode, statBuf.st_mode, EXIT);
    ICUNIT_GOTO_NOT_EQUAL(lstatBuf.st_mode & S_IFREG, 0, lstatBuf.st_mode, EXIT);

    ret = unlink(tmpFileName);
    ICUNIT_ASSERT_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
EXIT:
    (void)unlink(tmpFileName);
    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LSTAT_ENOENT
 * @tc.name     lstat
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLstatENOENT, Function | MediumTest | Level1)
{
    int32_t ret;
    struct stat buf = { 0 };

    /* 挂载点内不存在的文件 */
    ret = lstat(TEST_ROOT"/no_such_lstat_file", &buf);
    ICUNIT_ASSERT_EQUAL(errno, ENOENT, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_LSTAT_EINVAL
 * @tc.name     lstat
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsLstatEINVAL, Function | MediumTest | Level1)
{
    int32_t ret;
    struct stat buf = { 0 };
    const char tmpFileName[TEST_BUF_SIZE] = { FILE1 };

    /* path 为 NULL */
    ret = lstat(NULL, &buf);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    /* path 为空字符串 */
    ret = lstat("", &buf);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    /* buffer 为 NULL */
    ret = lstat(tmpFileName, NULL);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

void PosixFsLstatTest(void)
{
    RUN_ONE_TESTCASE(TestFsLstatOK);
    RUN_ONE_TESTCASE(TestFsLstatENOENT);
    RUN_ONE_TESTCASE(TestFsLstatEINVAL);
}
