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
#include <sys/uio.h>

/* *
 * @tc.number   SUB_KERNEL_FS_READV_OK
 * @tc.name     readv
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsReadvOK, Function | MediumTest | Level1)
{
    int32_t ret;
    ssize_t size;
    off_t offset;
    const char tmpFileName[TEST_BUF_SIZE] = { FILE1 };
    char writeBuf[TEST_BUF_SIZE] = "0123456789";
    char buf1[TEST_RW_SIZE] = { 0 };
    char buf2[TEST_RW_SIZE] = { 0 };
    struct iovec iov[2] = { 0 };

    (void)remove(tmpFileName);
    int32_t fd = open(tmpFileName, O_CREAT | O_RDWR, TEST_MODE_HIGH);
    ICUNIT_ASSERT_NOT_EQUAL(fd, POSIX_FS_IS_ERROR, fd);

    size = write(fd, writeBuf, strlen(writeBuf));
    ICUNIT_GOTO_NOT_EQUAL(size, POSIX_FS_IS_ERROR, size, EXIT);

    offset = lseek(fd, 0, SEEK_SET);
    ICUNIT_GOTO_EQUAL((int)offset, 0, (int)offset, EXIT);

    /* 分段读: 第一段 4 字节 + 第二段 6 字节 */
    iov[0].iov_base = buf1;
    iov[0].iov_len = 4; /* 4, first segment length */
    iov[1].iov_base = buf2;
    iov[1].iov_len = 6; /* 6, second segment length */

    size = readv(fd, iov, 2); /* 2, iov count */
    ICUNIT_GOTO_EQUAL(size, strlen(writeBuf), size, EXIT);

    ret = strncmp(buf1, "0123", 4); /* 4, first segment length */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = strncmp(buf2, "456789", 6); /* 6, second segment length */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

EXIT:
    ret = close(fd);
    ICUNIT_GOTO_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT1);

EXIT1:
    ret = unlink(tmpFileName);
    ICUNIT_ASSERT_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_READV_PARTIAL
 * @tc.name     readv
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsReadvPartial, Function | MediumTest | Level1)
{
    int32_t ret;
    ssize_t size;
    off_t offset;
    const char tmpFileName[TEST_BUF_SIZE] = { FILE1 };
    char writeBuf[TEST_BUF_SIZE] = "0123456789";
    char buf1[TEST_RW_SIZE] = { 0 };
    char buf2[TEST_RW_SIZE] = { 0 };
    struct iovec iov[2] = { 0 };

    (void)remove(tmpFileName);
    int32_t fd = open(tmpFileName, O_CREAT | O_RDWR, TEST_MODE_HIGH);
    ICUNIT_ASSERT_NOT_EQUAL(fd, POSIX_FS_IS_ERROR, fd);

    size = write(fd, writeBuf, strlen(writeBuf));
    ICUNIT_GOTO_NOT_EQUAL(size, POSIX_FS_IS_ERROR, size, EXIT);

    offset = lseek(fd, 0, SEEK_SET);
    ICUNIT_GOTO_EQUAL((int)offset, 0, (int)offset, EXIT);

    /* 请求长度(8 + 8)超过文件长度(10), 第二段只填充剩余 2 字节 */
    iov[0].iov_base = buf1;
    iov[0].iov_len = 8; /* 8, first segment length */
    iov[1].iov_base = buf2;
    iov[1].iov_len = 8; /* 8, second segment length */

    size = readv(fd, iov, 2); /* 2, iov count */
    ICUNIT_GOTO_EQUAL(size, strlen(writeBuf), size, EXIT);

    ret = strncmp(buf1, "01234567", 8); /* 8, first segment length */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = strncmp(buf2, "89", 2); /* 2, valid bytes in second segment */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

EXIT:
    ret = close(fd);
    ICUNIT_GOTO_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT1);

EXIT1:
    ret = unlink(tmpFileName);
    ICUNIT_ASSERT_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_READV_EINVAL
 * @tc.name     readv
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsReadvEINVAL, Function | MediumTest | Level1)
{
    int32_t ret;
    const char tmpFileName[TEST_BUF_SIZE] = { FILE1 };
    char buf1[TEST_RW_SIZE] = { 0 };
    char buf2[TEST_RW_SIZE] = { 0 };
    struct iovec iov[2] = { 0 };
    struct iovec zeroIov[2] = { 0 };

    int32_t fd = open(tmpFileName, O_CREAT | O_RDWR, TEST_MODE_HIGH);
    ICUNIT_ASSERT_NOT_EQUAL(fd, POSIX_FS_IS_ERROR, fd);

    iov[0].iov_base = buf1;
    iov[0].iov_len = TEST_RW_SIZE;
    iov[1].iov_base = buf2;
    iov[1].iov_len = TEST_RW_SIZE;

    /* iov 为 NULL */
    ret = readv(fd, NULL, 2); /* 2, iov count */
    ICUNIT_GOTO_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);

    /* iovcnt 为 0 */
    ret = readv(fd, iov, 0);
    ICUNIT_GOTO_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);

    /* iovcnt 超过 IOV_MAX_CNT(4) */
    ret = readv(fd, iov, 5); /* 5, iov count over IOV_MAX_CNT */
    ICUNIT_GOTO_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);

    /* 所有段长度均为 0 */
    zeroIov[0].iov_base = buf1;
    zeroIov[0].iov_len = 0;
    zeroIov[1].iov_base = buf2;
    zeroIov[1].iov_len = 0;
    ret = readv(fd, zeroIov, 2); /* 2, iov count */
    ICUNIT_GOTO_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);

    /* 非法 fd */
    ret = readv(POSIX_FS_IS_ERROR, iov, 2); /* 2, iov count */
    ICUNIT_GOTO_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT);

EXIT:
    ret = close(fd);
    ICUNIT_GOTO_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret, EXIT1);

EXIT1:
    ret = unlink(tmpFileName);
    ICUNIT_ASSERT_NOT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

void PosixFsReadvTest(void)
{
    RUN_ONE_TESTCASE(TestFsReadvOK);
    RUN_ONE_TESTCASE(TestFsReadvPartial);
    RUN_ONE_TESTCASE(TestFsReadvEINVAL);
}
