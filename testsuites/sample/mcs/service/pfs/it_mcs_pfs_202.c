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

/* 用例简要描述: ItMcsPfs202 pfs 目录族：建目录遍历改名状态与递归清理 */
static UINT32 Testcase(VOID)
{
    uint8_t wbuf[16];
    union {
        uint8_t bytes[512];
        uint64_t align; /* 保证 getdents64 记录起始 8 字节对齐 */
    } dbuf;
    struct pfs_stat st;
    const struct pfs_dirent64 *de;
    uint32_t pos = 0;
    int fd = -1;
    int df = -1;
    int found = 0;
    int ret;
    uint32_t i;

    for (i = 0; i < sizeof(wbuf); i++) {
        wbuf[i] = (uint8_t)(i + 1);
    }

    ret = pfs_mkdir("/mnt/pfs_d1", 0755);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = pfs_open("/mnt/pfs_d1/a.txt", 64 | 2, 0644); /* O_CREAT|O_RDWR */
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);
    ret = pfs_write(fd, wbuf, sizeof(wbuf));
    ICUNIT_GOTO_EQUAL(ret, 16, ret, EXIT);
    ret = pfs_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = -1;

    ret = pfs_stat("/mnt/pfs_d1", &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL((st.mode & 0x4000) != 0, 1, st.mode, EXIT); /* S_IFDIR */

    df = pfs_open("/mnt/pfs_d1", 0, 0); /* O_RDONLY 打开目录 */
    ICUNIT_GOTO_EQUAL(df >= 0, 1, df, EXIT);
    ret = pfs_getdents(df, dbuf.bytes, sizeof(dbuf.bytes));
    ICUNIT_GOTO_EQUAL(ret > 0, 1, ret, EXIT);
    /* PfsGetdentsNext 按 d_reclen 步进，遍历应找到 "a.txt" */
    while ((de = PfsGetdentsNext(dbuf.bytes, (uint32_t)ret, &pos)) != NULL) {
        if (strcmp(de->d_name, "a.txt") == 0) {
            found = 1;
        }
    }
    ICUNIT_GOTO_EQUAL(found, 1, found, EXIT);
    ret = pfs_close(df);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    df = -1;

    ret = pfs_rename("/mnt/pfs_d1/a.txt", "/mnt/pfs_d1/b.txt");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pfs_access("/mnt/pfs_d1/b.txt", 0); /* F_OK */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pfs_unlink("/mnt/pfs_d1/b.txt");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pfs_rmdir("/mnt/pfs_d1");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    /* 目录已删：再 rmdir 应 ENOENT */
    ret = pfs_rmdir("/mnt/pfs_d1");
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)pfs_close(fd);
    }
    if (df >= 0) {
        (void)pfs_close(df);
    }
    (void)pfs_unlink("/mnt/pfs_d1/b.txt");
    (void)pfs_unlink("/mnt/pfs_d1/a.txt");
    (void)pfs_rmdir("/mnt/pfs_d1");
    return LOS_NOK;
}

VOID ItMcsPfs202(VOID)
{
    TEST_ADD_CASE("ItMcsPfs202", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL0, TEST_FUNCTION);
}
