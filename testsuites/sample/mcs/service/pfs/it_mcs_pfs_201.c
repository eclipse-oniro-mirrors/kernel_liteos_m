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

/* 用例简要描述: ItMcsPfs201 pfs 文件全生命周期：创建写入读回定位状态清理 */
static UINT32 Testcase(VOID)
{
    uint8_t wbuf[256];
    uint8_t rbuf[256];
    struct pfs_stat st;
    int64_t pos;
    int fd = -1;
    int ret;
    uint32_t i;

    for (i = 0; i < sizeof(wbuf); i++) {
        wbuf[i] = (uint8_t)(i + 1);
    }
    (void)memset(rbuf, 0, sizeof(rbuf));

    fd = pfs_open("/mnt/pfs_t1", 64 | 2, 0644); /* O_CREAT|O_RDWR */
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);
    ret = pfs_write(fd, wbuf, sizeof(wbuf));
    ICUNIT_GOTO_EQUAL(ret, 256, ret, EXIT);
    pos = pfs_lseek(fd, 0, 0); /* SEEK_SET 回到文件头 */
    ICUNIT_GOTO_EQUAL(pos, 0, (int)pos, EXIT);
    ret = pfs_read(fd, rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(ret, 256, ret, EXIT);
    ret = memcmp(rbuf, wbuf, sizeof(wbuf));
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pfs_fstat(fd, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL((uint32_t)st.size, 256, (int)st.size, EXIT);
    ret = pfs_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = -1;
    ret = pfs_unlink("/mnt/pfs_t1");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    /* 已删除：无 O_CREAT 再 open 应 ENOENT */
    ret = pfs_open("/mnt/pfs_t1", 0, 0); /* O_RDONLY */
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)pfs_close(fd);
    }
    (void)pfs_unlink("/mnt/pfs_t1");
    return LOS_NOK;
}

VOID ItMcsPfs201(VOID)
{
    TEST_ADD_CASE("ItMcsPfs201", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL0, TEST_FUNCTION);
}
