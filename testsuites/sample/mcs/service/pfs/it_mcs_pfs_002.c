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

/* 用例简要描述: ItMcsPfs002 pfs_fcntl F_GETFL/F_SETFL 标志直通 */
static UINT32 Testcase(VOID)
{
    int fd = -1;
    int fl;
    int ret;

    fd = pfs_open("/mnt/pfs_t10", 64 | 2, 0644); /* O_CREAT|O_RDWR */
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);
    fl = pfs_fcntl(fd, 3, 0); /* F_GETFL */
    ICUNIT_GOTO_EQUAL(fl >= 0, 1, fl, EXIT);
    ret = pfs_fcntl(fd, 4, (uint32_t)(fl | 2048)); /* F_SETFL 追加 O_NONBLOCK */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fl = pfs_fcntl(fd, 3, 0); /* 回读应带 O_NONBLOCK */
    ICUNIT_GOTO_EQUAL((fl & 2048) != 0, 1, fl, EXIT);
    ret = pfs_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = -1;
    ret = pfs_unlink("/mnt/pfs_t10");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)pfs_close(fd);
    }
    (void)pfs_unlink("/mnt/pfs_t10");
    return LOS_NOK;
}

VOID ItMcsPfs002(VOID)
{
    TEST_ADD_CASE("ItMcsPfs002", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL1, TEST_FUNCTION);
}
