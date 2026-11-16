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

/* 用例简要描述: ItMcsSystem002 np_system 命令落盘与 pfs_read 回读组合验证 */
static UINT32 Testcase(VOID)
{
    uint8_t buf[64];
    int fd = -1;
    int ret;

    (void)memset(buf, 0, sizeof(buf));
    /* 主核 system() 执行重定向落盘，从核经 pfs 路径回读校验 */
    ret = np_system("echo proxysvc_ok > /mnt/np_sys_t1.txt");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = pfs_open("/mnt/np_sys_t1.txt", 0, 0); /* O_RDONLY */
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);
    ret = pfs_read(fd, buf, sizeof(buf) - 1); /* 留 1B 保 NUL 终止供 strstr 检索 */
    ICUNIT_GOTO_EQUAL(ret > 0, 1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(strstr((const char *)buf, "proxysvc_ok") != NULL, 1, ret, EXIT);
    ret = pfs_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = -1;
    ret = pfs_unlink("/mnt/np_sys_t1.txt");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* cmd=NULL：从核侧 -EINVAL */
    ret = np_system(NULL);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)pfs_close(fd);
    }
    (void)pfs_unlink("/mnt/np_sys_t1.txt");
    return LOS_NOK;
}

VOID ItMcsSystem002(VOID)
{
    TEST_ADD_CASE("ItMcsSystem002", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_MICA, TEST_LEVEL0, TEST_FUNCTION);
}
