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

#include "posix_fs_test.h"

/* *
 * @tc.number   SUB_KERNEL_FS_ACCESS_OK
 * @tc.name     access
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsAccessOK, Function | MediumTest | Level1)
{
    int32_t ret;
    const char tmpFileName[TEST_BUF_SIZE] = FILE1;

    int32_t fd = open(tmpFileName, O_CREAT | O_RDWR, TEST_MODE_HIGH);
    ICUNIT_ASSERT_NOT_EQUAL(fd, POSIX_FS_IS_ERROR, fd);

    ret = access(tmpFileName, F_OK);
    ICUNIT_GOTO_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);

#if (LOSCFG_SUPPORT_FATFS == 1)
    ret = access(tmpFileName, R_OK);
    ICUNIT_GOTO_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);

    ret = access(tmpFileName, W_OK);
    ICUNIT_GOTO_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);

    ret = access(tmpFileName, X_OK);
    ICUNIT_GOTO_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);
#endif

EXIT:
    ret = close(fd);
    ICUNIT_ASSERT_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    ret = unlink(tmpFileName);
    ICUNIT_ASSERT_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_ACCESS_EINVAL
 * @tc.name     access
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsAccessEINVAL, Function | MediumTest | Level1)
{
    char *tmpFileName = NULL;

    int32_t ret = access(tmpFileName, F_OK);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_ACCESS_MODEMATRIX
 * @tc.name     access
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsAccessModeMatrix, Function | MediumTest | Level1)
{
    int32_t ret;
    struct stat buf = { 0 };
    const char tmpFileName[TEST_BUF_SIZE] = { FILE1 };

    (void)remove(tmpFileName);
    int32_t fd = open(tmpFileName, O_CREAT | O_RDWR, TEST_MODE_HIGH);
    ICUNIT_ASSERT_NOT_EQUAL(fd, POSIX_FS_IS_ERROR, fd);

    ret = close(fd);
    ICUNIT_GOTO_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);

    /* F_OK: 文件存在即返回 0 */
    ret = access(tmpFileName, F_OK);
    ICUNIT_GOTO_EQUAL(ret, POSIX_FS_NO_ERROR, ret, EXIT);

    /* R_OK/W_OK/X_OK 按 vfs 实现校验真实行为: 与 stat 返回的权限位保持一致
     * (littlefs 的 stat 不填充权限位, R/W/X 均应返回 EACCES) */
    ret = stat(tmpFileName, &buf);
    ICUNIT_GOTO_EQUAL(ret, POSIX_FS_NO_ERROR, ret, EXIT);

    ret = access(tmpFileName, R_OK);
    if (buf.st_mode & (S_IRUSR | S_IRGRP | S_IROTH)) {
        ICUNIT_GOTO_EQUAL(ret, POSIX_FS_NO_ERROR, ret, EXIT);
    } else {
        ICUNIT_GOTO_EQUAL(errno, EACCES, POSIX_FS_IS_ERROR, EXIT);
        ICUNIT_GOTO_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);
    }

    ret = access(tmpFileName, W_OK);
    if (buf.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) {
        ICUNIT_GOTO_EQUAL(ret, POSIX_FS_NO_ERROR, ret, EXIT);
    } else {
        ICUNIT_GOTO_EQUAL(errno, EACCES, POSIX_FS_IS_ERROR, EXIT);
        ICUNIT_GOTO_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);
    }

    ret = access(tmpFileName, X_OK);
    if (buf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
        ICUNIT_GOTO_EQUAL(ret, POSIX_FS_NO_ERROR, ret, EXIT);
    } else {
        ICUNIT_GOTO_EQUAL(errno, EACCES, POSIX_FS_IS_ERROR, EXIT);
        ICUNIT_GOTO_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);
    }

    /* 组合 mode: 请求位全部满足才返回 0 */
    ret = access(tmpFileName, F_OK | R_OK | W_OK);
    if ((buf.st_mode & (S_IRUSR | S_IRGRP | S_IROTH)) &&
        (buf.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))) {
        ICUNIT_GOTO_EQUAL(ret, POSIX_FS_NO_ERROR, ret, EXIT);
    } else {
        ICUNIT_GOTO_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);
    }

    /* 不存在路径: stat 失败透传 ENOENT */
    ret = access(TEST_ROOT"/no_such_access_file", F_OK);
    ICUNIT_GOTO_EQUAL(errno, ENOENT, POSIX_FS_IS_ERROR, EXIT);
    ICUNIT_GOTO_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);

    ret = unlink(tmpFileName);
    ICUNIT_ASSERT_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
EXIT:
    (void)unlink(tmpFileName);
    return POSIX_FS_NO_ERROR;
}

void PosixFsAccessTest(void)
{
    RUN_ONE_TESTCASE(TestFsAccessOK);
    RUN_ONE_TESTCASE(TestFsAccessEINVAL);
    RUN_ONE_TESTCASE(TestFsAccessModeMatrix);
}
