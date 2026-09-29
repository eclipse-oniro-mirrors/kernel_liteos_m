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
#include "los_fs.h"

/* 测试专用未挂载路径, 确保不与实际挂载点冲突 */
#define FS_MOUNT_TEST_MP       TEST_ROOT"/fs_mount_test_mp"
#define FS_MOUNT_OUT_MP        "/fs_mount_out_mp"

/* *
 * @tc.number   SUB_KERNEL_FS_MOUNT_EINVAL
 * @tc.name     mount
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsMountEINVAL, Function | MediumTest | Level1)
{
    int32_t ret;

    /* target 为 NULL */
    ret = mount(NULL, NULL, "littlefs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    /* target 不以 '/' 开头 */
    ret = mount(NULL, "fs_mount_no_slash", "littlefs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_MOUNT_ALREADY_MOUNTED
 * @tc.name     mount
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsMountAlreadyMounted, Function | MediumTest | Level1)
{
    int32_t ret;

    /* 对已挂载的 TEST_ROOT 重复挂载, 在 VfsMountPointInit 中返回 EINVAL */
    ret = mount(NULL, TEST_ROOT, "littlefs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_MOUNT_ENODEV
 * @tc.name     mount
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsMountENODEV, Function | MediumTest | Level1)
{
    int32_t ret;

    /* 不支持的文件系统类型 */
    ret = mount(NULL, FS_MOUNT_OUT_MP, "no_such_fs_type", 0, NULL);
    ICUNIT_ASSERT_EQUAL(errno, ENODEV, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_MOUNT_ENAMETOOLONG
 * @tc.name     mount
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsMountENAMETOOLONG, Function | MediumTest | Level1)
{
    int32_t ret;
    /* 静态长缓冲, 避免占用任务栈 */
    static char longPath[PATH_MAX + 2] = { 0 };

    (void)memset_s(longPath, sizeof(longPath), 'a', sizeof(longPath) - 1);
    longPath[0] = '/';
    longPath[PATH_MAX + 1] = '\0'; /* 总长度 PATH_MAX + 1, 超过 PATH_MAX 限制 */

    ret = mount(NULL, longPath, "littlefs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(errno, ENAMETOOLONG, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_MOUNT_REMOUNT_EINVAL
 * @tc.name     mount
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsMountRemountEINVAL, Function | MediumTest | Level1)
{
    int32_t ret;

    /* MS_REMOUNT 到未挂载路径, VfsRemount 找不到挂载点 */
    ret = mount(NULL, FS_MOUNT_OUT_MP, "littlefs", MS_REMOUNT, NULL);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

#if (LOSCFG_SUPPORT_LITTLEFS == 1)
/* *
 * @tc.number   SUB_KERNEL_FS_MOUNT_EFAULT
 * @tc.name     mount
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsMountEFAULT, Function | MediumTest | Level1)
{
    int32_t ret;

    /* littlefs 挂载必须携带分区配置(data), data 为 NULL 时 LfsMount 返回 EFAULT */
    ret = mount(NULL, FS_MOUNT_OUT_MP, "littlefs", 0, NULL);
    ICUNIT_ASSERT_EQUAL(errno, EFAULT, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_MOUNT_REMOUNT_EFAULT
 * @tc.name     mount
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsMountRemountEFAULT, Function | MediumTest | Level1)
{
    int32_t ret;

    /* littlefs 对 MS_REMOUNT 且 data 为 NULL: LfsMount 先校验 data, 返回 EFAULT */
    ret = mount(NULL, TEST_ROOT, "littlefs", MS_REMOUNT, NULL);
    ICUNIT_ASSERT_EQUAL(errno, EFAULT, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}
#endif

/* *
 * @tc.number   SUB_KERNEL_FS_UMOUNT_EINVAL
 * @tc.name     umount
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsUmountEINVAL, Function | MediumTest | Level1)
{
    int32_t ret;

    /* target 为 NULL */
    ret = umount(NULL);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    /* target 不以 '/' 开头 */
    ret = umount("fs_mount_no_slash");
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_UMOUNT_ENOENT
 * @tc.name     umount
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsUmountENOENT, Function | MediumTest | Level1)
{
    int32_t ret;

    /* 未挂载路径, 该错误分支不设置 errno, 只校验返回值 */
    ret = umount(FS_MOUNT_OUT_MP);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_UMOUNT2_EINVAL
 * @tc.name     umount2
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsUmount2EINVAL, Function | MediumTest | Level1)
{
    int32_t ret;

    /* target 为 NULL */
    ret = umount2(NULL, 0);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    /* target 不以 '/' 开头 */
    ret = umount2("fs_mount_no_slash", 0);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, POSIX_FS_IS_ERROR);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

/* *
 * @tc.number   SUB_KERNEL_FS_UMOUNT2_ENOENT
 * @tc.name     umount2
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsUmount2ENOENT, Function | MediumTest | Level1)
{
    int32_t ret;

    /* 未挂载路径, 该错误分支不设置 errno, 只校验返回值 */
    ret = umount2(FS_MOUNT_OUT_MP, 0);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}

#if (LOSCFG_SUPPORT_LITTLEFS == 1)
/* *
 * @tc.number   SUB_KERNEL_FS_UMOUNT2_NOT_SUPPORT
 * @tc.name     umount2
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixFsFuncTestSuite, TestFsUmount2NotSupport, Function | MediumTest | Level1)
{
    int32_t ret;
    struct stat buf = { 0 };

    /* littlefs 适配层未实现 umount2 操作, 对已挂载点调用返回失败 */
    ret = umount2(TEST_ROOT, 0);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    ret = umount2(TEST_ROOT, MNT_FORCE);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_IS_ERROR, ret);

    /* 确认挂载点未被卸载, 仍可正常访问 */
    ret = stat(TEST_ROOT, &buf);
    ICUNIT_ASSERT_EQUAL(ret, POSIX_FS_NO_ERROR, ret);

    return POSIX_FS_NO_ERROR;
}
#endif

void PosixFsMountTest(void)
{
    RUN_ONE_TESTCASE(TestFsMountEINVAL);
    RUN_ONE_TESTCASE(TestFsMountAlreadyMounted);
    RUN_ONE_TESTCASE(TestFsMountENODEV);
    RUN_ONE_TESTCASE(TestFsMountENAMETOOLONG);
    RUN_ONE_TESTCASE(TestFsMountRemountEINVAL);
#if (LOSCFG_SUPPORT_LITTLEFS == 1)
    RUN_ONE_TESTCASE(TestFsMountEFAULT);
    RUN_ONE_TESTCASE(TestFsMountRemountEFAULT);
#endif
    RUN_ONE_TESTCASE(TestFsUmountEINVAL);
    RUN_ONE_TESTCASE(TestFsUmountENOENT);
    RUN_ONE_TESTCASE(TestFsUmount2EINVAL);
    RUN_ONE_TESTCASE(TestFsUmount2ENOENT);
#if (LOSCFG_SUPPORT_LITTLEFS == 1)
    RUN_ONE_TESTCASE(TestFsUmount2NotSupport);
#endif
}
