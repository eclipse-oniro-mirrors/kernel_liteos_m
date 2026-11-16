/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "it_mcs.h"

/* 用例简要描述: ItMcsPfs001 pfs 入参无效族：空路径超长路径不存在句柄双重关闭 */
static UINT32 Testcase(VOID)
{
    char longPath[301]; /* "/mnt/" + 295*'a' = 300 字符，超从核 PFS_PATH_MAX(256) */
    int64_t pos;
    int fd = -1;
    int ret;

    /* path=NULL：从核侧 -EINVAL */
    ret = pfs_open(NULL, 2, 0); /* O_RDWR */
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    (void)memset(longPath, 'a', sizeof(longPath));
    longPath[sizeof(longPath) - 1] = '\0';
    (void)memcpy(longPath, "/mnt/", 5);

    /* 超长路径：从核侧 -ENAMETOOLONG */
    ret = pfs_open(longPath, 64 | 2, 0644); /* O_CREAT|O_RDWR */
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 不存在句柄：主核 EBADF 转回 */
    ret = pfs_close(-1);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    /* 合法建 → close → 双重 close */
    fd = pfs_open("/mnt/pfs_t9", 64 | 2, 0644);
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);
    ret = pfs_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pfs_close(fd);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    fd = -1;

    pos = pfs_lseek(999999, 0, 0); /* 不存在的大 fd */
    ICUNIT_GOTO_EQUAL((pos < 0), 1, (int)pos, EXIT);

    ret = pfs_unlink("/mnt/pfs_t9");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)pfs_close(fd); /* best-effort：可能已关，忽略返回值 */
    }
    (void)pfs_unlink("/mnt/pfs_t9");
    return LOS_NOK;
}

VOID ItMcsPfs001(VOID)
{
    TEST_ADD_CASE("ItMcsPfs001", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL0, TEST_FUNCTION);
}
