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

/* 用例简要描述: ItMcsPfs204 pfs 4096 字节分片写入读回数据全等 */
static UINT32 Testcase(VOID)
{
    static uint8_t wbuf[4096]; /* 大缓冲放静态区，避免测试任务栈溢出 */
    static uint8_t rbuf[4096];
    int64_t pos;
    int fd = -1;
    int ret;
    uint32_t i;

    for (i = 0; i < sizeof(wbuf); i++) {
        wbuf[i] = (uint8_t)(i ^ 0xA5);
    }
    (void)memset(rbuf, 0, sizeof(rbuf));

    fd = pfs_open("/mnt/pfs_t4", 64 | 2, 0644); /* O_CREAT|O_RDWR */
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);
    /* RPC_MAX_PAYLOAD=448：4096B 写在从核侧按 440B/片分片转发，应累计写满 */
    ret = pfs_write(fd, wbuf, sizeof(wbuf));
    ICUNIT_GOTO_EQUAL(ret, 4096, ret, EXIT);
    pos = pfs_lseek(fd, 0, 0); /* SEEK_SET */
    ICUNIT_GOTO_EQUAL(pos, 0, (int)pos, EXIT);
    /* 读回走 resp 分片重组，应一次拿满 4096B */
    ret = pfs_read(fd, rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(ret, 4096, ret, EXIT);
    ret = memcmp(rbuf, wbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pfs_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    fd = -1;
    ret = pfs_unlink("/mnt/pfs_t4");
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)pfs_close(fd);
    }
    (void)pfs_unlink("/mnt/pfs_t4");
    return LOS_NOK;
}

VOID ItMcsPfs204(VOID)
{
    TEST_ADD_CASE("ItMcsPfs204", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL0, TEST_FUNCTION);
}
